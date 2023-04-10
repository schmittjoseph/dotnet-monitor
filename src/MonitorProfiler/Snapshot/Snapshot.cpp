// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "Snapshot.h"
#include <functional>
#include <memory>

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

static volatile bool s_keepHooks = true;

Snapshot::Snapshot(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo)
{
    m_pLogger = logger;
    m_pCorProfilerInfo = profilerInfo;
    m_enabled = TRUE;
}


HRESULT Snapshot::UpdateEventMask(DWORD deltaLowMask, DWORD deltaHighMask, BOOL invert)
{
    HRESULT hr = S_OK;
    DWORD eventsLow = 0;
    DWORD eventsHigh = 0;

    IfFailLogRet(m_pCorProfilerInfo->GetEventMask2(&eventsLow, &eventsHigh));

    if (invert) {
        eventsLow &= ~deltaLowMask;
        eventsHigh &= ~deltaHighMask;
    } else {
        eventsLow |= deltaLowMask;
        eventsHigh |= deltaHighMask;
    }

    IfFailLogRet(m_pCorProfilerInfo->SetEventMask2(eventsLow, eventsHigh));

    return S_OK;
}

HRESULT Snapshot::Enable()
{
    m_pLogger->Log(LogLevel::Warning, _LS("Enabling snapshotter"));
    m_enabled = TRUE;
    return UpdateEventMask(COR_PRF_MONITOR_ENTERLEAVE, m_dwHighEventMask, FALSE);
}


HRESULT Snapshot::Disable()
{
    m_pLogger->Log(LogLevel::Warning, _LS("Disabling snapshotter"));
    // s_keepHooks = FALSE;
    m_enabled = FALSE;
    return UpdateEventMask(COR_PRF_MONITOR_ENTERLEAVE, m_dwHighEventMask, TRUE);
}

HRESULT Snapshot::Toggle()
{
    HRESULT hr = S_OK;
    if (m_enabled) {
        hr = Disable();
    } else {
        hr = Enable();
    }

    m_pLogger->Log(LogLevel::Warning, _LS("hr: %0x"), hr);

    return hr;
}

UINT_PTR FunctionMapper(FunctionID functionId, BOOL *pbHookFunction)
{
    *pbHookFunction = s_keepHooks;
    return functionId;
}


void Snapshot::AddProfilerEventMask(DWORD& eventsLow)
{
    eventsLow |= m_dwLowEventMask;

    // m_pCorProfilerInfo->SetFunctionIDMapper(&FunctionMapper);
}

#define SHORT_LENGTH    32

mdMethodDef Snapshot::GetMethodDefForFunction(FunctionID functionId)
{
    ClassID classId = NULL;
    ModuleID moduleId = NULL;
    mdToken token = NULL;
    ULONG32 nTypeArgs = NULL;
    ClassID typeArgs[SHORT_LENGTH];
    COR_PRF_FRAME_INFO frameInfo = NULL;

    HRESULT hr = S_OK;
    hr = m_pCorProfilerInfo->GetFunctionInfo2(functionId,
                                            frameInfo,
                                            &classId,
                                            &moduleId,
                                            &token,
                                            SHORT_LENGTH,
                                            &nTypeArgs,
                                            typeArgs);
    if (FAILED(hr))
    {
        printf("Call to GetFunctionInfo2 failed with hr=0x%x\n", hr);
        return mdTokenNil;
    }

    return token;
}

ModuleID Snapshot::GetModuleIDForFunction(FunctionID functionId)
{
    ClassID classId = NULL;
    ModuleID moduleId = NULL;
    mdToken token = NULL;
    ULONG32 nTypeArgs = NULL;
    ClassID typeArgs[SHORT_LENGTH];
    COR_PRF_FRAME_INFO frameInfo = NULL;

    HRESULT hr = S_OK;
    hr = m_pCorProfilerInfo->GetFunctionInfo2(functionId,
                                            frameInfo,
                                            &classId,
                                            &moduleId,
                                            &token,
                                            SHORT_LENGTH,
                                            &nTypeArgs,
                                            typeArgs);
    return moduleId;
}

HRESULT STDMETHODCALLTYPE Snapshot::EnterCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    HRESULT hr = S_OK;

    if (!m_enabled) {
        return S_OK;
    }


    // Turn off
    // m_pLogger->Log(LogLevel::Debug, _LS("Enter Hook!"));

// Removes ELT hooks from a function. Works but "no guarentees".
/*
    if (!m_enabled) {

        ModuleID targetModuleId = GetModuleIDForFunction(functionId.functionID);
        mdMethodDef targetMethodDef = GetMethodDefForFunction(functionId.functionID);
        // Still running a function with the hooks jitted in. Request a rejit to revet them.
        m_pLogger->Log(LogLevel::Warning, _LS("Rejitting method to remove hooks!"));
        if (s_remappedID == 0) {
            s_remappedID = functionId.functionID;
        }
        IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(COR_PRF_REJIT_BLOCK_INLINING, 1, &targetModuleId, &targetMethodDef));
    }
*/

/*
    ThreadID threadId;
    IfFailLogRet(m_pCorProfilerInfo->GetCurrentThreadID(&threadId));

    
    DataMapIterator iterator = m_pShadowStacks.find(threadId);
    IfFalseLogRet(iterator != _dataMap.end(), E_UNEXPECTED);

    threadData = iterator->second;

    auto foo = m_pPartialStack->cbegin();
    FunctionID nextFunctionWeWant = *foo;

    if (functionId.functionID == nextFunctionWeWant) {
        // We're here. Advance the iterator and wait for the next function.
        foo++;
    } else if (foo != m_pPartialStack->) {
        // Werent even tracking a shadow stack, no-op. 
        foo = m_pPartialStack->cbegin();
    } else {
        // Swing-and-a-miss, reset the shadow stack.
        // --- not true, we need to track pops to determine resetting it.
        // We need a list so that we 
    }

    // https://github.com/dotnet/runtime/issues/10797#issuecomment-727042054
    // All volatile registers will be preserved when using SetEnterLeaveFunctionHooks3.
    //
    // However, in .net 7+ errno is **not** preserved.
    // ref: https://github.com/dotnet/runtime/issues/79546

    // DWORD lastError = GetLastError();
    // SetLastError(lastError);

    // We need to go **fast**, use a combination of a dirty cache and a cache line to avoid needing to take any locks
    // in our callbacks.


    // Grab the cache-line.

    // Thread created, update the dirty cache.

*/
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Snapshot::LeaveCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    // if not matching partial stack, no-op.
    // If we are *AND* we're leaving the current, pop up on the list.
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Snapshot::TailcallCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    return S_OK;
}

#ifdef PROTOTYPE
#define SHORT_LENGTH    32
#define STRING_LENGTH  256
#define LONG_LENGTH   1024

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

String Snapshot::GetFunctionIDName(FunctionID funcId)
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

HRESULT STDMETHODCALLTYPE Snapshot::EnterCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
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

HRESULT STDMETHODCALLTYPE Snapshot::LeaveCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    DWORD lastError = GetLastError();

    if (IsUserCode(functionId.functionID)) {
        String functionName = GetFunctionIDName(functionId.functionID);
        printf("[leave] %S\n", functionName.ToCStr());
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE Snapshot::TailcallCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo)
{
    DWORD lastError = GetLastError();

    if (IsUserCode(functionId.functionID)) {
        String functionName = GetFunctionIDName(functionId.functionID);
        printf("[tail]  %S\n", functionName.ToCStr());
    }

    SetLastError(lastError);
    return S_OK;
}
#endif