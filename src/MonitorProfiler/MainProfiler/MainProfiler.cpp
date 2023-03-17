// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#define NOMINMAX
#include "MainProfiler.h"
#include "../Environment/EnvironmentHelper.h"
#include "../Environment/ProfilerEnvironment.h"
#include "../Logging/AggregateLogger.h"
#include "../Logging/DebugLogger.h"
#include "../Logging/StdErrLogger.h"
#include "../Stacks/StacksEventProvider.h"
#include "../Stacks/StackSampler.h"
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

    std::wcout << "[profiler] Initializing" << std::endl;

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

    *pbNotificationOnly = !m_isMainProfiler;

    return S_OK;
}

#define PROFILER_STUB static void STDMETHODCALLTYPE
#define SHORT_LENGTH    32
#define STRING_LENGTH  256
#define LONG_LENGTH   1024

PROFILER_STUB EnterStub(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    MainProfiler::s_profiler->EnterCallback(functionId, eltInfo);
}

PROFILER_STUB LeaveStub(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    MainProfiler::s_profiler->LeaveCallback(functionId, eltInfo);
}

PROFILER_STUB TailcallStub(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    MainProfiler::s_profiler->TailcallCallback(functionId, eltInfo);
}

HRESULT MainProfiler::InitializeCommon()
{
    HRESULT hr = S_OK;

    // These are created in dependency order!
    IfFailRet(InitializeEnvironment());
    IfFailRet(InitializeLogging());
    IfFailRet(InitializeEnvironmentHelper());

    // Decide what kind of profiler we will be.
    BOOL isMainProfiler = FALSE;
    if (SUCCEEDED(m_pEnvironmentHelper->GetIsMainProfiler(isMainProfiler))) {
        m_isMainProfiler = isMainProfiler;
    }

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
    IfFailLogRet(m_pEnvironmentHelper->SetProductVersion());

    DWORD eventsLow = COR_PRF_MONITOR::COR_PRF_MONITOR_NONE;
#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    ThreadDataManager::AddProfilerEventMask(eventsLow);
    _exceptionTracker->AddProfilerEventMask(eventsLow);
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS
    StackSampler::AddProfilerEventMask(eventsLow);

    _threadNameCache = make_shared<ThreadNameCache>();

// JSFIX
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_FUNCTION_ARGS | COR_PRF_ENABLE_FUNCTION_RETVAL | COR_PRF_ENABLE_FRAME_INFO | COR_PRF_MONITOR_ENTERLEAVE;

    IfFailRet(m_pCorProfilerInfo->SetEventMask2(
        eventsLow,
        COR_PRF_HIGH_MONITOR::COR_PRF_HIGH_MONITOR_NONE));

    //Initialize this last. The CommandServer creates secondary threads, which will be difficult to cleanup if profiler initialization fails.
    IfFailLogRet(InitializeCommandServer());

    // JSFIX
    if (m_isMainProfiler) {
        m_pCorProfilerInfo->SetEnterLeaveFunctionHooks3WithInfo(
            &EnterStub,
            &LeaveStub,
            &TailcallStub
        );
    }

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


String MainProfiler::GetClassIDName(ClassID classId)
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
        return WCHAR("");
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
        return WCHAR("ArrayClass");
    }
    else if (CORPROF_E_CLASSID_IS_COMPOSITE == hr)
    {
        // We have a composite class
        return WCHAR("CompositeClass");
    }
    else if (CORPROF_E_DATAINCOMPLETE == hr)
    {
        // type-loading is not yet complete. Cannot do anything about it.
        return WCHAR("DataIncomplete");
    }
    else if (FAILED(hr))
    {
        printf("FAIL: GetClassIDInfo returned 0x%x for ClassID %x\n", hr, (unsigned int)classId);
        return WCHAR("GetClassIDNameFailed");
    }

    ComPtr<IMetaDataImport> pMDImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(modId,
                                             (ofRead | ofWrite),
                                             IID_IMetaDataImport,
                                             (IUnknown **)&pMDImport );
    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return WCHAR("ClassIDLookupFailed");
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
        return WCHAR("ClassIDLookupFailed");
    }

    String name = wName;
    if (nTypeArgs > 0)
        name += WCHAR("<");

    for(ULONG32 i = 0; i < nTypeArgs; i++)
    {

        String typeArgClassName;
        typeArgClassName.Clear();
        name += GetClassIDName(typeArgs[i]);

        if ((i + 1) != nTypeArgs)
            name += WCHAR(", ");
    }

    if (nTypeArgs > 0)
        name += WCHAR(">");

    return name;
}

BOOL MainProfiler::IsUserCode(FunctionID funcId)
{
    ClassID classId = NULL;
    ModuleID moduleId = NULL;
    mdToken token = NULL;

    HRESULT hr = S_OK;

    if (!m_seeUserModule) {
        return false;
    }


    hr = m_pCorProfilerInfo->GetFunctionInfo(funcId, &classId, &moduleId, &token);

    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return false;
    }

    // return m_moduleId == moduleId;
    return m_classId == classId && m_moduleId == moduleId;
}

HRESULT MainProfiler::CacheUserModuleId(FunctionID funcId)
{
    ClassID classId = NULL;
    ModuleID moduleId = NULL;
    mdToken token = NULL;

    HRESULT hr = S_OK;
    hr = m_pCorProfilerInfo->GetFunctionInfo(funcId, &classId, &moduleId, &token);
#ifdef SLOW
    ULONG32 nTypeArgs = NULL;
    ClassID typeArgs[SHORT_LENGTH];
    COR_PRF_FRAME_INFO frameInfo = NULL;
    hr = m_pCorProfilerInfo->GetFunctionInfo2(funcId,
                                            frameInfo,
                                            &classId,
                                            &moduleId,
                                            &token,
                                            SHORT_LENGTH,
                                            &nTypeArgs,
                                            typeArgs);
#endif
    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return hr;
    }

    m_classId = classId;
    m_moduleId = moduleId;
    m_seeUserModule = true;

    return S_OK;
}

String MainProfiler::GetFunctionIDName(FunctionID funcId)
{
    // If the FunctionID is 0, we could be dealing with a native function.
    if (funcId == 0)
    {
        return WCHAR("Unknown_Native_Function");
    }

    String name;

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
        return WCHAR("FuncNameLookupFailed");
    }

    ComPtr<IMetaDataImport> pIMDImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(moduleId,
                                             ofRead,
                                             IID_IMetaDataImport,
                                             (IUnknown **)&pIMDImport);
    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return WCHAR("FuncNameLookupFailed");
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
        return WCHAR("FuncNameLookupFailed");
    }


    if (classId != NULL) {
        name += GetClassIDName(classId);
        name += L"!";
    }

    name += funcName;

    // Fill in the type parameters of the generic method
    if (nTypeArgs > 0)
        name += WCHAR("<");

    for(ULONG32 i = 0; i < nTypeArgs; i++)
    {
        name += GetClassIDName(typeArgs[i]);

        if ((i + 1) != nTypeArgs)
            name += WCHAR(", ");
    }

    if (nTypeArgs > 0)
        name += WCHAR(">");

    return name;
}

bool hasEnding (std::wstring const &fullString, std::wstring const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

HRESULT STDMETHODCALLTYPE MainProfiler::EnterCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    // https://github.com/dotnet/runtime/issues/10797#issuecomment-727042054
    // All volatile registers will be preserved when using SetEnterLeaveFunctionHooks3.
    //
    // However, in .net 7+ errno is **not** preserved.
    // ref: https://github.com/dotnet/runtime/issues/79546
    SetLastError(0);
    HRESULT hr = S_OK;
    String functionName;
    if (!m_seeUserModule) {
        functionName = GetFunctionIDName(functionId.functionID);
        if (hasEnding(functionName.ToWString(), L"!Main")) {
            printf("[profiler] Calculated user code heuristic\n");
            CacheUserModuleId(functionId.functionID);
#ifdef AUTO_TURN_OFF_ELT_HOOKS
            printf("[profiler] Turning off elt hooks.\n");
            DWORD eventsLow = COR_PRF_MONITOR::COR_PRF_MONITOR_NONE;
            eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_FUNCTION_ARGS | COR_PRF_ENABLE_FUNCTION_RETVAL | COR_PRF_ENABLE_FRAME_INFO;
            IfFailRet(m_pCorProfilerInfo->SetEventMask2(
                eventsLow,
                COR_PRF_HIGH_MONITOR::COR_PRF_HIGH_MONITOR_NONE));
#endif
        } else {
            return S_OK;
        }
    } else if(IsUserCode(functionId.functionID)) {
        functionName = GetFunctionIDName(functionId.functionID);
    } else {
        return S_OK;
    }

    COR_PRF_FRAME_INFO pFrameInfo;
    ULONG pcbArgumentInfo = 0;
    hr = m_pCorProfilerInfo->GetFunctionEnter3Info(functionId.functionID, eltInfo, &pFrameInfo, &pcbArgumentInfo, NULL);
    COR_PRF_FUNCTION_ARGUMENT_INFO* pArgumentInfo = (COR_PRF_FUNCTION_ARGUMENT_INFO*)_alloca(pcbArgumentInfo);
    IfFailRet(m_pCorProfilerInfo->GetFunctionEnter3Info(functionId.functionID, eltInfo, &pFrameInfo, &pcbArgumentInfo, pArgumentInfo));

    printf("[enter] %S(", functionName.ToCStr());
    for (uint64_t i = 0; i < pArgumentInfo->numRanges; i++) {
        COR_PRF_FUNCTION_ARGUMENT_RANGE range = pArgumentInfo->ranges[i];
        if (i != 0) {
            printf(", ");
        };

        byte** nullCheck = reinterpret_cast<byte**>(range.startAddress);
        if (*nullCheck == NULL) {
            printf("null");
            continue;
        };

        if (range.length == sizeof(int)) {
            int value = *(int *)range.startAddress;
            printf("%d", value);
            continue;
        } else if (range.length == (sizeof(bool))) {
            bool value = *(bool *)range.startAddress;
            if (value == 0) {
                printf("false");
            } else {
                printf("true");
            }
            continue;
        } else {
            // For now assume it's a string
            ULONG lengthOffset = 0;
            ULONG bufferOffset = 0;
            IfFailRet(m_pCorProfilerInfo->GetStringLayout2(&lengthOffset, &bufferOffset));

            UINT_PTR lengthReference = *((UINT_PTR *)range.startAddress) + lengthOffset;
            DWORD strLength = *(DWORD *)lengthReference;

            UINT_PTR strReference = *((UINT_PTR *)range.startAddress) + bufferOffset;
            WCHAR *strPtr = (WCHAR *)strReference;

            printf("\"%S\"", strPtr);
        }
    }

    printf(")\n");

/*
    for (uint64_t i = 0; i < pArgumentInfo->numRanges; i++) {
        COR_PRF_FUNCTION_ARGUMENT_RANGE range = pArgumentInfo->ranges[i];
                printf(
                "      startAddress: %p\n      length: %u\n",
                (void *) range.startAddress,
                range.length
        );

        printf("      data:");
        for (UINT_PTR index = 0; index < range.length; index++) {
            uint8_t byte = *(((uint8_t *) range.startAddress) + index);
            printf(" %02x", byte);
        }
        printf("\n");
    }
*/
    if (hasEnding(functionName.ToWString(), L"!ProfilerBlock")) {
        printf("[block] BLOCKING EXECUTION OF THREAD!\n");
        ThreadUtilities::Sleep(10 * 1000);
        printf("[block] RESUMING EXECUTION OF THREAD!\n");
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MainProfiler::LeaveCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    DWORD lastError = GetLastError();

    if (IsUserCode(functionId.functionID)) {
        String functionName = GetFunctionIDName(functionId.functionID);
        printf("[leave] %S\n", functionName.ToCStr());
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MainProfiler::TailcallCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    DWORD lastError = GetLastError();

    if (IsUserCode(functionId.functionID)) {
        String functionName = GetFunctionIDName(functionId.functionID);
        printf("[tail]  %S\n", functionName.ToCStr());
    }

    SetLastError(lastError);
    return S_OK;
}
