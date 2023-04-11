// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "Snapshot.h"
#include <functional>
#include <memory>
#include "ILRewriter.h"

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

shared_ptr<Snapshot> Snapshot::s_snapshotter;

Snapshot::Snapshot(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo)
{
    m_pLogger = logger;
    m_pCorProfilerInfo = profilerInfo;
    m_enabled = FALSE;
    Snapshot::s_snapshotter = shared_ptr<Snapshot>(this);
}

HRESULT Snapshot::Enable(FunctionID enterHookId, FunctionID leaveHookId, FunctionID funcId)
{
    HRESULT hr = S_OK;

    m_enterHookId = enterHookId;
    m_leaveHookId = leaveHookId;

    m_pLogger->Log(LogLevel::Warning, _LS("Enabling snapshotter"));

    // Resolve the module id and method def.
    mdMethodDef methodDef = GetMethodDefForFunction(funcId);
    ModuleID modId = GetModuleIDForFunction(funcId);

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        1,
        &modId,
        &methodDef));

    m_enabled = TRUE;
    return S_OK;
}


HRESULT Snapshot::Disable(FunctionID funcId)
{
    HRESULT hr = S_OK;

    m_pLogger->Log(LogLevel::Warning, _LS("Disabling snapshotter"));

    mdMethodDef methodDef = GetMethodDefForFunction(funcId);
    ModuleID modId = GetModuleIDForFunction(funcId);

    IfFailLogRet(m_pCorProfilerInfo->RequestRevert(1, &modId, &methodDef, nullptr));

    // s_keepHooks = FALSE;
    m_enabled = FALSE;

    return S_OK;
}

HRESULT Snapshot::Toggle(FunctionID enterHookId, FunctionID leaveHookId, FunctionID funcId)
{
    HRESULT hr = S_OK;
    if (m_enabled) {
        hr = Disable(funcId);
    } else {
        hr = Enable(enterHookId, leaveHookId, funcId);
    }

    return hr;
}

void Snapshot::AddProfilerEventMask(DWORD& eventsLow)
{
    m_pLogger->Log(LogLevel::Debug, _LS("Configuring snapshotter."));
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT | COR_PRF_MONITOR_JIT_COMPILATION;
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

#define PROFILER_STUB static void STDMETHODCALLTYPE
PROFILER_STUB EnterStub(FunctionID functionId)
{
    Snapshot::s_snapshotter->EnterCallback(functionId);
}

PROFILER_STUB LeaveStub(FunctionID functionId)
{
    Snapshot::s_snapshotter->LeaveCallback(functionId);
}

COR_SIGNATURE enterLeaveMethodSignature             [] = { IMAGE_CEE_CS_CALLCONV_STDCALL, 0x01, ELEMENT_TYPE_VOID, ELEMENT_TYPE_I };
void(STDMETHODCALLTYPE *EnterMethodAddress)(FunctionID) = &EnterStub;
void(STDMETHODCALLTYPE *LeaveMethodAddress)(FunctionID) = &LeaveStub;

HRESULT STDMETHODCALLTYPE Snapshot::EnterCallback(FunctionID functionId)
{
    HRESULT hr = S_OK;

    // Turn off
    m_pLogger->Log(LogLevel::Debug, _LS("Enter Hook!"));

    return S_OK;
}

HRESULT STDMETHODCALLTYPE Snapshot::ReJITHandler(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr = S_OK;
    FunctionID functionId;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromToken(moduleId,
                                                methodId,
                                                &functionId));

    ComPtr<IMetaDataImport> metadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport)));

    ComPtr<IMetaDataEmit> metadataEmit;
    IfFailLogRet(metadataImport->QueryInterface(IID_IMetaDataEmit, reinterpret_cast<void **>(&metadataEmit)));


// #define NATIVE_HOOK
#ifdef NATIVE_HOOK

    mdSignature enterLeaveMethodSignatureToken;
    IfFailLogRet(metadataEmit->GetTokenFromSig(enterLeaveMethodSignature, sizeof(enterLeaveMethodSignature), &enterLeaveMethodSignatureToken));

    IfFailLogRet(RewriteIL(
        m_pCorProfilerInfo,
        pFunctionControl,
        moduleId,
        methodId,
        functionId,
        reinterpret_cast<ULONGLONG>(EnterMethodAddress),
        reinterpret_cast<ULONGLONG>(LeaveMethodAddress),
        enterLeaveMethodSignatureToken,
        TRUE));
#else

    mdMethodDef enterDef = GetMethodDefForFunction(m_enterHookId);
    mdMethodDef leaveDef = GetMethodDefForFunction(m_leaveHookId);

    IfFailLogRet(RewriteIL(
        m_pCorProfilerInfo,
        pFunctionControl,
        moduleId,
        methodId,
        functionId,
        enterDef,
        leaveDef,
        enterDef,
        FALSE));
#endif
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Snapshot::LeaveCallback(FunctionID functionId)
{
    m_pLogger->Log(LogLevel::Debug, _LS("Leave Hook!"));

    // if not matching partial stack, no-op.
    // If we are *AND* we're leaving the current, pop up on the list.
    return S_OK;
}
