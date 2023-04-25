// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "Snapshot.h"
#include <functional>
#include <memory>
#include "InsertProbes.h"
#include "../Utilities/NameCache.h"
#include "MethodSigParamExtractor.h"
#include "../Utilities/TypeNameUtilities.h"

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

#define ENUM_BUFFER_SIZE 10

Snapshot::Snapshot(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo)
{
    m_pLogger = logger;
    m_pCorProfilerInfo = profilerInfo;
    m_resolvedCorLibId = 0;
    m_enterHookId = 0;
    m_leaveHookId = 0;
    _isRejitting = false;
}

HRESULT Snapshot::RegisterFunctionProbes(FunctionID enterProbeID, FunctionID leaveProbeID)
{
    if (IsReady())
    {
        // Probes have already been pinned.
        return E_FAIL;
    }

    m_enterHookId = enterProbeID;
    m_leaveHookId = leaveProbeID;

    m_pLogger->Log(LogLevel::Information, _LS("Probes received"));

    return S_OK;
}

HRESULT Snapshot::RequestFunctionProbeShutdown()
{
    HRESULT hr;

    if (!IsReady())
    {
        return S_FALSE;
    }

    m_pLogger->Log(LogLevel::Information, _LS("Uninstall probes requested"));
    // JSFIX: Queue this work to run on *our* native-only thread.
    // JSFIX: Block until probes are truly gone.

    IfFailLogRet(Disable());
    return S_OK;
}

BOOL Snapshot::IsReady()
{
    return m_enterHookId != 0 && m_leaveHookId != 0;
}

HRESULT Snapshot::Enable()
{
    HRESULT hr = S_OK;

    if (!IsReady() || IsEnabled())
    {
        return E_FAIL;
    }

    _isRejitting = true;

    m_pLogger->Log(LogLevel::Warning, _LS("Enabling snapshotter"));

    // JSFIX: Using leave hook as the test func.
    FunctionID funcId = m_leaveHookId;

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

    IfFailLogRet(PrepareAssemblyForProbes(moduleId, methodDef));

    m_EnabledModuleIds.push_back(moduleId);
    m_EnabledMethodDefs.push_back(methodDef);

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)m_EnabledModuleIds.size(),
        m_EnabledModuleIds.data(),
        m_EnabledMethodDefs.data()));

    return S_OK;
}

HRESULT Snapshot::PrepareAssemblyForProbes(ModuleID moduleId, mdMethodDef methodId)
{
    HRESULT hr;

    auto const& it = m_ModuleTokens.find(moduleId);
    if (it != m_ModuleTokens.end())
    {
        return S_OK;
    }

    ComPtr<IMetaDataImport> pMetadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(
        moduleId,
        ofRead | ofWrite,
        IID_IMetaDataImport,
        reinterpret_cast<IUnknown **>(&pMetadataImport)));

    ComPtr<IMetaDataEmit> pMetadataEmit;
    IfFailLogRet(pMetadataImport->QueryInterface(IID_IMetaDataEmit, reinterpret_cast<void **>(&pMetadataEmit)));

    struct CorLibTypeTokens corLibTypeTokens;
    IfFailLogRet(EmitNecessaryCorLibTypeTokens(pMetadataImport, pMetadataEmit, &corLibTypeTokens));
    m_ModuleTokens[moduleId] = corLibTypeTokens;

    return S_OK;
}


HRESULT Snapshot::Disable()
{
    HRESULT hr = S_OK;

    if (!IsEnabled() || _isRejitting)
    {
        return S_FALSE;
    }

    m_pLogger->Log(LogLevel::Warning, _LS("Disabling snapshotter"));

    // TODO: Check output status
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

HRESULT Snapshot::GetMethodDefForFunction(FunctionID functionId, mdMethodDef* pMethodDef)
{
    HRESULT hr;
    *pMethodDef = mdTokenNil;

    hr = m_pCorProfilerInfo->GetFunctionInfo2(functionId,
                                            NULL,
                                            NULL,
                                            NULL,
                                            pMethodDef,
                                            0,
                                            NULL,
                                            NULL);

    return S_OK;
}

HRESULT Snapshot::DumpArgs(ModuleID moduleId, mdMethodDef methodDef)
{
    HRESULT hr = S_OK;
    ComPtr<IMetaDataImport> pMetadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pMetadataImport)));

    HCORENUM hEnum = 0;
    mdParamDef paramDefs[ENUM_BUFFER_SIZE];
    ULONG count = 0;
    ULONG index = 0;
    while ((hr = pMetadataImport->EnumParams(&hEnum, methodDef, paramDefs, ENUM_BUFFER_SIZE, &count)) == S_OK)
    {
        for (ULONG i = 0; i < count; i++)
        {
            mdMethodDef md2;
            ULONG sequence;
            WCHAR paramName[256];
            ULONG nameLength;
            DWORD flags;
            DWORD dwCPlusFlags;
            void const *pValue;
            ULONG cbValue;

            IfFailLogRet(pMetadataImport->GetParamProps(paramDefs[i], &md2, &sequence, paramName, 256, &nameLength, &flags, &dwCPlusFlags, &pValue, &cbValue));
            m_pLogger->Log(LogLevel::Warning, _LS("[%u] %s"), sequence, paramName);
            index++;
        }
    }

    return S_OK;
}

HRESULT Snapshot::DumpArgs2(ModuleID moduleId, mdMethodDef methodDef)
{
    HRESULT hr = S_OK;
    MethodSigParamExtractor extractor;

    ComPtr<IMetaDataImport> pMetadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pMetadataImport)));

    PCCOR_SIGNATURE sigParam;
    ULONG cbSigParam;
    IfFailLogRet(pMetadataImport->GetMethodProps(methodDef, NULL, NULL, 0, NULL, NULL, &sigParam, &cbSigParam, NULL, NULL));


    if (!extractor.Parse((sig_byte *)sigParam, cbSigParam))
    {
        return E_FAIL;
    }
    
    m_pLogger->Log(LogLevel::Warning, _LS("Arg count: %d"), extractor.GetParamCount());

    return S_OK;
}


HRESULT STDMETHODCALLTYPE Snapshot::ReJITHandler(ModuleID moduleId, mdMethodDef methodDef, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr = S_OK;
    m_pLogger->Log(LogLevel::Warning, _LS("REJITTING: 0x%0x - 0x%0x."), moduleId, methodDef);

    struct CorLibTypeTokens typeTokens;
    auto const& it = m_ModuleTokens.find(moduleId);
    if (it == m_ModuleTokens.end())
    {
        return E_FAIL;
    }
    typeTokens = it->second;

    IfFailLogRet(DumpArgs(moduleId, methodDef));

    FunctionID functionId;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromToken(moduleId,
                                                methodDef,
                                                &functionId));

    ComPtr<IMetaDataImport> pMetadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pMetadataImport)));

    PCCOR_SIGNATURE sigParam;
    ULONG cbSigParam;
    IfFailLogRet(pMetadataImport->GetMethodProps(methodDef, NULL, NULL, 0, NULL, NULL, &sigParam, &cbSigParam, NULL, NULL));

    mdMethodDef enterDef;
    mdMethodDef leaveDef;
    IfFailLogRet(GetMethodDefForFunction(m_enterHookId, &enterDef));
    IfFailLogRet(GetMethodDefForFunction(m_leaveHookId, &leaveDef));

    IfFailLogRet(InsertProbes(
        m_pCorProfilerInfo,
        pMetadataImport,
        pFunctionControl,
        moduleId,
        methodDef,
        functionId,
        enterDef,
        leaveDef,
        sigParam,
        cbSigParam,
        &typeTokens));

    m_pLogger->Log(LogLevel::Warning, _LS("DONE."));

    // Fix: Always set this, even on error.
    _isRejitting = false;

    return S_OK;
}

HRESULT Snapshot::ResolveCorLib(ModuleID *pCorLibModuleId)
{
    if (m_resolvedCorLibId != 0)
    {
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
    while (pEnum->Next(1, &curModule, NULL) == S_OK)
    {
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

        if (curMetadataImporter->FindTypeDefByName(_T("System.Object"), mdTokenNil, &tkObjectTypeDef) != S_OK)
        {
            continue;
        }

        DWORD dwClassAttrs = 0;
        mdToken tkExtends = mdTokenNil;
        if (curMetadataImporter->GetTypeDefProps(tkObjectTypeDef, NULL, NULL, 0, &dwClassAttrs, &tkExtends) != S_OK)
        {
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

    if (sysObjectTypeDef == mdTypeDefNil)
    {
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
            if (hr == CLDB_S_TRUNCATION)
            {
                continue;
            }
            else if (hr != S_OK)
            {
                return hr;
            }

            if (cchName != expectedLength)
            {
                continue;
            }

            tstring assemblyNameStr = tstring(assemblyName);

            if (assemblyNameStr == expectedName) {
                pMetadataAssemblyImport->CloseEnum(hEnum);
                *pTkMscorlibAssemblyRef = curRef;
                return S_OK;
            }
        }
    }

    if (hEnum)
    {
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

#define GET_OR_DEFINE_TYPE_TOKEN(name, pToken) \
    IfFailRet(GetTokenForType( \
        pMetadataImport, \
        pMetadataEmit, \
        tkCorlibAssemblyRef, \
        name, \
        pToken))

    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Boolean"), &pCorLibTypeTokens->tkSystemBooleanType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Byte"), &pCorLibTypeTokens->tkSystemByteType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Char"), &pCorLibTypeTokens->tkSystemCharType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Double"), &pCorLibTypeTokens->tkSystemDoubleType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Int16"), &pCorLibTypeTokens->tkSystemInt16Type);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Int32"), &pCorLibTypeTokens->tkSystemInt32Type);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Int64"), &pCorLibTypeTokens->tkSystemInt64Type);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Object"), &pCorLibTypeTokens->tkSystemObjectType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.SByte"), &pCorLibTypeTokens->tkSystemSByteType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.Single"), &pCorLibTypeTokens->tkSystemSingleType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.String"), &pCorLibTypeTokens->tkSystemStringType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.UInt16"), &pCorLibTypeTokens->tkSystemUInt16Type);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.UInt32"), &pCorLibTypeTokens->tkSystemUInt32Type);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.UInt64"), &pCorLibTypeTokens->tkSystemUInt64Type);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.IntPtr"), &pCorLibTypeTokens->tkSystemIntPtrType);
    GET_OR_DEFINE_TYPE_TOKEN(_T("System.UIntPtr"), &pCorLibTypeTokens->tkSystemUIntPtrType);

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
        IfFailRet(pMetadataEmit->DefineTypeRefByName(
            tkResolutionScope,
            name.c_str(),
            &tkType));
    }

    *ptkType = tkType;
    return S_OK;
}