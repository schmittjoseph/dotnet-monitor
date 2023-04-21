// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "Snapshot.h"
#include <functional>
#include <memory>
#include "InsertProbes.h"
#include "../Utilities/NameCache.h"
#include "../Utilities/TypeNameUtilities.h"

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

#define ENUM_BUFFER_SIZE 10

Snapshot::Snapshot(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo)
{
    m_pLogger = logger;
    m_pCorProfilerInfo = profilerInfo;
    m_resolvedCorLibId = 0;
    _rejitDidFinish = false;
}

HRESULT Snapshot::RegisterFunctionProbes(FunctionID enterProbeID, FunctionID leaveProbeID)
{
    m_enterHookId = enterProbeID;
    m_leaveHookId = leaveProbeID;

    m_pLogger->Log(LogLevel::Information, _LS("Probes received"));

    return S_OK;
}

HRESULT Snapshot::ResolveAllProbes()
{
    return S_OK;
}

HRESULT Snapshot::Enable(tstring name)
{
    HRESULT hr = S_OK;

    IfFailLogRet(ResolveAllProbes());

    m_pLogger->Log(LogLevel::Warning, _LS("Enabling snapshotter"));

    // JSFIX: Using leave hook as the test func.
    FunctionID funcId = m_leaveHookId;
    // JSFIX: Ensure we jit the function first before attempting a rejit.

    const long numberOfFunctions = 1;

    ModuleID moduleId;
    mdToken methodDef;

    IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(funcId,
                                            NULL,
                                            NULL,
                                            &moduleId,
                                            &methodDef,
                                            0,
                                            NULL,
                                            NULL));


    // Now wait for the rejits to occur.
    std::unique_lock<std::mutex> lock(_rejitMutex);
    _rejitDidFinish = false;
    m_EnabledModuleIds.push_back(moduleId);
    m_EnabledMethodDefs.push_back(methodDef);

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)m_EnabledModuleIds.size(),
        m_EnabledModuleIds.data(),
        m_EnabledMethodDefs.data()));
    
    _rejitFinished.wait(lock, [this]() { return _rejitDidFinish; });

    // Store the module ids & method defs so we know what needs to be backed out.
    m_pLogger->Log(LogLevel::Warning, _LS("Done"));

    return S_OK;
}


HRESULT Snapshot::Disable()
{
    HRESULT hr = S_OK;

    std::lock_guard<std::mutex> lock(_rejitMutex);
    m_pLogger->Log(LogLevel::Warning, _LS("Disabling snapshotter"));

    IfFailLogRet(m_pCorProfilerInfo->RequestRevert(
        (ULONG)m_EnabledModuleIds.size(),
        m_EnabledModuleIds.data(),
        m_EnabledMethodDefs.data(),
        nullptr));

    m_EnabledModuleIds.clear();
    m_EnabledMethodDefs.clear();
    m_pLogger->Log(LogLevel::Warning, _LS("Done"));

    return S_OK;
}

BOOL Snapshot::IsEnabled() {
    return m_EnabledModuleIds.size() != 0;
}

void Snapshot::AddProfilerEventMask(DWORD& eventsLow)
{
    m_pLogger->Log(LogLevel::Debug, _LS("Configuring snapshotter."));
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT;
}

mdMethodDef Snapshot::GetMethodDefForFunction(FunctionID functionId)
{
    mdToken token = mdTokenNil;

    HRESULT hr = S_OK;
    // JSFIX: Check ret
    hr = m_pCorProfilerInfo->GetFunctionInfo2(functionId,
                                            NULL,
                                            NULL,
                                            NULL,
                                            &token,
                                            0,
                                            NULL,
                                            NULL);

    return token;
}

HRESULT STDMETHODCALLTYPE Snapshot::ReJITHandler(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr = S_OK;
    m_pLogger->Log(LogLevel::Warning, _LS("REJITTING: 0x%0x - 0x%0x."), moduleId, methodId);

#pragma region resolve_method_defs

    FunctionID functionId;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromToken(moduleId,
                                                methodId,
                                                &functionId));
    m_pLogger->Log(LogLevel::Warning, _LS("--> 1 - 0x%0x"), functionId);

    ComPtr<IMetaDataImport> metadataImport;
    try {
        ComPtr<IUnknown> test;
        HRESULT unknownHr = 0x1e41430;

        //  facility: 0x1e4 code: 0x1430
        //  facility: 484 code: 5168
        
        // 0x80131430

        // internal error:0x80131506

        // May return this unknown hr, or cascade and cause COR_E_EXECUTIONENGINE
        hr = m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, &test);
        if (hr == unknownHr) {
            m_pLogger->Log(LogLevel::Warning, _LS("--> INVALID STATE REACHED - 0x%0x"), hr);
            return hr;
        } else if (hr != S_OK) {
            m_pLogger->Log(LogLevel::Warning, _LS("--> WHAT - 0x%0x"), hr);
            return hr;
        }

        hr = m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport));
        if (hr != S_OK) {
            return hr;
        }

        m_pLogger->Log(LogLevel::Warning, _LS("--> 2"));
    } catch (...) {
        m_pLogger->Log(LogLevel::Warning, _LS("--> 2 -- EXCEPTION"));
    }

    PCCOR_SIGNATURE sigParam;
    ULONG cbSigParam;
    IfFailLogRet(metadataImport->GetMethodProps(methodId, NULL, NULL, 0, NULL, NULL, &sigParam, &cbSigParam, NULL, NULL));
    m_pLogger->Log(LogLevel::Warning, _LS("--> 3"));

    ComPtr<IMetaDataEmit> metadataEmit;
    IfFailLogRet(metadataImport->QueryInterface(IID_IMetaDataEmit, reinterpret_cast<void **>(&metadataEmit)));
    m_pLogger->Log(LogLevel::Warning, _LS("--> 4"));



    mdMethodDef enterDef = GetMethodDefForFunction(m_enterHookId);
    mdMethodDef leaveDef = GetMethodDefForFunction(m_leaveHookId);
    m_pLogger->Log(LogLevel::Warning, _LS("--> 5"));

#pragma endregion


    struct CorLibTypeTokens tokens;
    IfFailRet(EmitNecessaryCorLibTypeTokens(metadataImport, metadataEmit, &tokens));
    m_pLogger->Log(LogLevel::Warning, _LS("--> 6"));

    IfFailLogRet(InsertProbes(
        m_pCorProfilerInfo,
        pFunctionControl,
        moduleId,
        methodId,
        functionId,
        enterDef,
        leaveDef,
        sigParam,
        cbSigParam,
        &tokens));
    m_pLogger->Log(LogLevel::Warning, _LS("DONE."));

    // Fix: Always set this, even on error.
    _rejitDidFinish = true;
    _rejitFinished.notify_all();

    return S_OK;
}

HRESULT Snapshot::ResolveCorLib(ModuleID *pCorLibModuleId)
{
    if (m_resolvedCorLibId != 0) {
        *pCorLibModuleId = m_resolvedCorLibId;
        return S_OK;
    }

    HRESULT hr = S_OK;
    *pCorLibModuleId = 0;
    ModuleID candidateModuleId = 0;
    ComPtr<ICorProfilerModuleEnum> pEnum = NULL;
    ModuleID curModule;
    mdTypeDef sysObjectTypeDef = mdTypeDefNil;

    IfFailLogRet(m_pCorProfilerInfo->EnumModules(&pEnum));
    while (pEnum->Next(1, &curModule, NULL) == S_OK) {
        //
        
        // In the CoreCLR with reference assemblies and redirection it is difficult to determine if
        // a particular Assembly is the System assembly.
        //
        // In the CoreCLR runtimes, the System assembly can be System.Private.CoreLib.dll, System.Runtime.dll
        // or netstandard.dll and in the future a different Assembly name could be used.
        //
        // Determine the identity of the System assembly by querying if the Assembly defines the
        // well known type System.Object as that type must be defined by the System assembly
        //
        mdTypeDef tkObjectTypeDef = mdTypeDefNil;

        ComPtr<IMetaDataImport> curMetadataImporter;
        IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(curModule, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&curMetadataImporter)));

        if (curMetadataImporter->FindTypeDefByName(_T("System.Object"), mdTokenNil, &tkObjectTypeDef) != S_OK) {
            continue;
        }

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
            candidateModuleId = curModule;
            sysObjectTypeDef = tkObjectTypeDef;
            break;
        }
    }

    if (sysObjectTypeDef == mdTypeDefNil) {
        return E_FAIL;
    }

    WCHAR moduleFullName[256];
    ULONG nameLength = 0;
    AssemblyID assemblyID;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleInfo(candidateModuleId,
        nullptr,
        256,
        &nameLength,
        moduleFullName,
        &assemblyID));
    

    m_pLogger->Log(LogLevel::Warning, _LS("CORLIB: %s"), moduleFullName);

    *pCorLibModuleId = m_resolvedCorLibId = candidateModuleId;
    return S_OK;
}


HRESULT Snapshot::GetTokenForExistingCorLibAssemblyRef(ComPtr<IMetaDataImport> pMetadataImport, ComPtr<IMetaDataEmit> pMetadataEmit, mdAssemblyRef* pTkMscorlibAssemblyRef)
{
    
    // #define NETCORECORLIB _T("System.Private.CoreLib") // JSFIX: Not true,  need to calculate this from ResolveCorLib
    #define NETCORECORLIB _T("System.Runtime")
    #define NETCORECORLIB_NAME_LENGTH (sizeof(NETCORECORLIB)/sizeof(WCHAR))

    HRESULT hr = S_OK;
    *pTkMscorlibAssemblyRef = mdAssemblyRefNil;

    // ModuleID corlibID;
    // IfFailLogRet(ResolveCorLib(&corlibID));

    ComPtr<IMetaDataAssemblyImport> pMetadataAssemblyImport;
    IfFailLogRet(pMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pMetadataAssemblyImport)));

    HCORENUM hEnum = 0;
    mdAssemblyRef mdRefs[ENUM_BUFFER_SIZE];
    ULONG count = 0;

    const tstring expectedName = tstring(NETCORECORLIB);

    // We only need a buffer of equal size to NETCORECORLIB.
    const ULONG expectedLength = NETCORECORLIB_NAME_LENGTH;
    TCHAR assemblyName[expectedLength]; 
    m_pLogger->Log(LogLevel::Debug, _LS("----> a "));  

    while ((hr = pMetadataAssemblyImport->EnumAssemblyRefs(&hEnum, mdRefs, ENUM_BUFFER_SIZE, &count)) == S_OK)
    {
        for (ULONG i = 0; i < count; i++)
        {
            mdAssemblyRef curRef = mdRefs[i];

            // Get the name.
            ULONG cchName = 0;
            hr = pMetadataAssemblyImport->GetAssemblyRefProps(
                curRef,
                NULL, NULL, // public key or token
                assemblyName, expectedLength, &cchName, // name
                NULL, // metadata`
                NULL, NULL, // hash value
                NULL); // flags

            // Current assembly's name is longer than corlib's
            if (hr == CLDB_S_TRUNCATION) {
                continue;
            } else if (hr != S_OK) {
                    m_pLogger->Log(LogLevel::Debug, _LS("----> b "));  

                return hr;
            }

            if (cchName != expectedLength) {
                continue;
            }

            tstring assemblyNameStr = tstring(assemblyName);

            if (assemblyNameStr == expectedName) {
                    m_pLogger->Log(LogLevel::Debug, _LS("----> c "));  

                pMetadataAssemblyImport->CloseEnum(hEnum);
                *pTkMscorlibAssemblyRef = curRef;
                return S_OK;
            }
        }
    }
                    m_pLogger->Log(LogLevel::Debug, _LS("----> d "));  

    if (hEnum) {
        pMetadataAssemblyImport->CloseEnum(hEnum);
    }

    /* First emit the corlib assembly ref */
    ComPtr<IMetaDataAssemblyEmit> pMetadataAssemblyEmit;
    IfFailLogRet(pMetadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));

    BYTE publicKeyToken[] = { 0x7c, 0xec, 0x85, 0xd7, 0xbe, 0xa7, 0x79, 0x8e };
    ASSEMBLYMETADATA corLibMetadata{};
    corLibMetadata.usMajorVersion = 4;

    IfFailLogRet(pMetadataAssemblyEmit->DefineAssemblyRef(
        publicKeyToken,
        8,
        NETCORECORLIB,
        &corLibMetadata,
        nullptr,
        0,
        afContentType_Default,
        pTkMscorlibAssemblyRef));
    m_pLogger->Log(LogLevel::Debug, _LS("!! Added new assembly ref!! "));  

    return S_OK;
}


HRESULT Snapshot::EmitNecessaryCorLibTypeTokens(
    ComPtr<IMetaDataImport> pMetadataImport,
    ComPtr<IMetaDataEmit> pMetadataEmit,
    struct CorLibTypeTokens *pCorLibTypeTokens)
{
    HRESULT hr = S_OK;

    mdAssemblyRef tkCorlibAssemblyRef = mdAssemblyRefNil;
    IfFailLogRet(GetTokenForExistingCorLibAssemblyRef(
        pMetadataImport,
        pMetadataEmit,
        &tkCorlibAssemblyRef));
/*
    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Boolean"),
        &pCorLibTypeTokens->tkSystemBooleanType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Byte"),
        &pCorLibTypeTokens->tkSystemByteType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Char"),
        &pCorLibTypeTokens->tkSystemCharType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Double"),
        &pCorLibTypeTokens->tkSystemDoubleType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Int16"),
        &pCorLibTypeTokens->tkSystemInt16Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Int32"),
        &pCorLibTypeTokens->tkSystemInt32Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Int64"),
        &pCorLibTypeTokens->tkSystemInt64Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Object"),
        &pCorLibTypeTokens->tkSystemObjectType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.SByte"),
        &pCorLibTypeTokens->tkSystemSByteType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.Single"),
        &pCorLibTypeTokens->tkSystemSingleType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.String"),
        &pCorLibTypeTokens->tkSystemStringType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.UInt16"),
        &pCorLibTypeTokens->tkSystemUInt16Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.UInt32"),
        &pCorLibTypeTokens->tkSystemUInt32Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.UInt64"),
        &pCorLibTypeTokens->tkSystemUInt64Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.IntPtr"),
        &pCorLibTypeTokens->tkSystemIntPtrType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkCorlibAssemblyRef,
        _T("System.UIntPtr"),
        &pCorLibTypeTokens->tkSystemUIntPtrType));
*/
    return S_OK;
}


HRESULT Snapshot::GetTokenForType(
    ComPtr<IMetaDataImport> pMetadataImport,
    ComPtr<IMetaDataEmit> pMetadataEmit,
    mdToken tkResolutionScope,
    tstring name,
    mdToken* ptkType)
{
    HRESULT hr = S_OK;

    *ptkType = mdTokenNil;

    mdTypeRef tkType;
    hr = pMetadataImport->FindTypeRef(
        tkResolutionScope,
        name.c_str(),
        &tkType);

    if (FAILED(hr) || tkType == mdTokenNil)
    {
        m_pLogger->Log(LogLevel::Debug, _LS("!! Added type ref :%s!! "), name);  

        IfFailRet(pMetadataEmit->DefineTypeRefByName(
            tkResolutionScope,
            name.c_str(),
            &tkType));
    }

    *ptkType = tkType;
    return S_OK;
}