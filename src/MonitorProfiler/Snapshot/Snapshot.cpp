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

HRESULT STDMETHODCALLTYPE Snapshot::ReJITHandler(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr = S_OK;

#pragma region resolve_method_defs

    FunctionID functionId;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromToken(moduleId,
                                                methodId,
                                                &functionId));

    ComPtr<IMetaDataImport> metadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport)));

    ComPtr<IMetaDataEmit> metadataEmit;
    IfFailLogRet(metadataImport->QueryInterface(IID_IMetaDataEmit, reinterpret_cast<void **>(&metadataEmit)));


    mdMethodDef enterDef = GetMethodDefForFunction(m_enterHookId);
    mdMethodDef leaveDef = GetMethodDefForFunction(m_leaveHookId);
#pragma endregion

#pragma region resolve_mscorlib
    // Find mscorlib
    ModuleID corLibId = 0;

    ComPtr<ICorProfilerModuleEnum> pEnum = NULL;
    ModuleID curModule;
    mdTypeDef sysObjectTypeDef = mdTypeDefNil;

    // ref: ildasm
    // https://github.com/dotnet/runtime/blob/887c043eb94be364188e2b23a87efa214ea57f1e/src/coreclr/ildasm/dasm.cpp#L876
    IfFailLogRet(m_pCorProfilerInfo->EnumModules(&pEnum));
    while (pEnum->Next(1, &curModule, NULL) == S_OK) {
        //
        // In the CoreCLR with reference assemblies and redirection it is more difficult to determine if
        // a particular Assembly is the System assembly, like mscorlib.dll is for the Desktop CLR.
        // In the CoreCLR runtimes, the System assembly can be System.Private.CoreLib.dll, System.Runtime.dll
        // or netstandard.dll and in the future a different Assembly name could be used.
        // We now determine the identity of the System assembly by querying if the Assembly defines the
        // well known type System.Object as that type must be defined by the System assembly
        //
        mdTypeDef tkObjectTypeDef = mdTypeDefNil;

        // Get the System.Object typedef
        ComPtr<IMetaDataImport> curMetadataImporter;
        IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(curModule, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&curMetadataImporter)));

        if (curMetadataImporter->FindTypeDefByName(_T("System.Object"), mdTokenNil, &tkObjectTypeDef) != S_OK) {
            continue;
        }

        // We have a type definition for System.Object in this assembly
        //
        DWORD dwClassAttrs = 0;
        mdToken tkExtends = mdTokenNil;
        if (curMetadataImporter->GetTypeDefProps(tkObjectTypeDef, NULL, NULL, 0, &dwClassAttrs, &tkExtends) != S_OK) {
            continue;
        }

        bool bExtends = curMetadataImporter->IsValidToken(tkExtends);
        bool isClass = ((dwClassAttrs & tdClassSemanticsMask) == tdClass);

        // We also check the type properties to make sure that we have a class and not a Value type definition
        // and that this type definition isn't extending another type.
        if (isClass & !bExtends)
        {
            m_pLogger->Log(LogLevel::Debug, _LS("Resolved base mscorlib.dll!System.Object assembly and type def 0x%0x"), tkObjectTypeDef);
            corLibId = curModule;
            sysObjectTypeDef = tkObjectTypeDef;
            break;
        }
    }

    if (sysObjectTypeDef == mdTypeDefNil) {
        m_pLogger->Log(LogLevel::Warning, _LS("Unable to resolve mscorlib.dll!System.Object!"));
        return E_FAIL;
    }

    if (moduleId == corLibId) {
        m_pLogger->Log(LogLevel::Warning, _LS("Refusing to patch mscorlib.dll!"));
        return E_FAIL;
    }

#pragma endregion

#pragma region emit_assembly_ref
#define NETCORECORLIB _T("System.Private.CoreLib")
#define NETCORECORASSEMBLYNAME NETCORECORLIB
    mdTypeRef inAssemblyTypeRef = mdTypeRefNil;
    ComPtr<IMetaDataAssemblyEmit> pMetadataAssemblyEmit;
    IfFailLogRet(metadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));

    BYTE publicKeyToken[] = { 0x7c, 0xec, 0x85, 0xd7, 0xbe, 0xa7, 0x79, 0x8e };
    ASSEMBLYMETADATA corLibMetadata{};
    corLibMetadata.usMajorVersion = 4;

    mdAssemblyRef corlibAssemblyRef = mdAssemblyRefNil;

    IfFailLogRet(pMetadataAssemblyEmit->DefineAssemblyRef(
        publicKeyToken,
        8,
        NETCORECORASSEMBLYNAME,
        &corLibMetadata,
        nullptr,
        0,
        afContentType_Default,
        &corlibAssemblyRef));
    m_pLogger->Log(LogLevel::Debug, _LS("Defined mscorlib.dll ref in target assembly."));

    // Need to emit a type ref
    IfFailLogRet(metadataEmit->DefineTypeRefByName(corlibAssemblyRef, _T("System.Object"), &inAssemblyTypeRef));
    m_pLogger->Log(LogLevel::Debug, _LS("Defined System.Object ref in target assembly."));

#pragma endregion

    IfFailLogRet(RewriteIL(
        m_pCorProfilerInfo,
        pFunctionControl,
        moduleId,
        methodId,
        functionId,
        enterDef,
        leaveDef,
        inAssemblyTypeRef,
        0));

    return S_OK;
}
