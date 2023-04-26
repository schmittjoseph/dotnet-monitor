// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "Snapshot.h"
#include <functional>
#include <memory>
#include "ProbeUtilities.h"
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
    m_enterProbeId = 0;
    _isRejitting = false;
    _isEnabled = false;
}

HRESULT Snapshot::RegisterFunctionProbe(FunctionID enterProbeId)
{
    if (IsAvailable())
    {
        // Probes have already been pinned.
        return E_FAIL;
    }

    m_enterProbeId = enterProbeId;

    m_pLogger->Log(LogLevel::Information, _LS("Probes received"));

    return S_OK;
}

HRESULT Snapshot::RequestFunctionProbeShutdown()
{
    HRESULT hr;

    if (!IsAvailable())
    {
        return S_FALSE;
    }

    m_pLogger->Log(LogLevel::Information, _LS("Uninstall probes requested"));
    // JSFIX: Queue this work to run on *our* native-only thread.
    // JSFIX: Block until probes are truly gone.

    IfFailLogRet(Disable());
    return S_OK;
}

HRESULT Snapshot::RequestFunctionProbeInstallation(UINT64 functionIds[], ULONG count)
{
    m_pLogger->Log(LogLevel::Information, _LS("Requesting probe installation."));

    // JSFIX: Thread safety.
    for (ULONG i = 0; i < count; i++)
    {
        m_RequestedFunctionIds.push_back((FunctionID)functionIds[i]);
    }

    return S_OK;
}

BOOL Snapshot::IsAvailable()
{
    return m_enterProbeId != 0;
}

HRESULT Snapshot::Enable()
{
    HRESULT hr = S_OK;

    if (!IsAvailable() || IsEnabled() || m_RequestedFunctionIds.size() == 0)
    {
        return E_FAIL;
    }

    for (ULONG i = 0; i < m_RequestedFunctionIds.size(); i++)
    {
        ModuleID moduleId;
        mdToken methodDef;

        IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(m_RequestedFunctionIds.at(i),
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
    }


    _isRejitting = true;

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)m_EnabledModuleIds.size(),
        m_EnabledModuleIds.data(),
        m_EnabledMethodDefs.data()));

    _isEnabled = true;
    m_RequestedFunctionIds.clear();

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

    _isEnabled = false;

    return S_OK;
}

BOOL Snapshot::IsEnabled() {
    return _isEnabled;
}

void Snapshot::AddProfilerEventMask(DWORD& eventsLow)
{
    m_pLogger->Log(LogLevel::Debug, _LS("Configuring snapshotter."));
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT;
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

    FunctionID functionId;

    // JSFIX: Generics
    //IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromTokenAndTypeArgs(moduleId, methodDef, mdTokenNil, 0, NULL, &functionId));
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromToken(moduleId,
                                                methodDef,
                                                &functionId));
    ComPtr<IMetaDataImport> pMetadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pMetadataImport)));

    PCCOR_SIGNATURE sigParam;
    ULONG cbSigParam;
    IfFailLogRet(pMetadataImport->GetMethodProps(methodDef, NULL, NULL, 0, NULL, NULL, &sigParam, &cbSigParam, NULL, NULL));

    mdMethodDef enterDef = mdTokenNil;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(m_enterProbeId,
                                                NULL,
                                                NULL,
                                                NULL,
                                                &enterDef,
                                                0,
                                                NULL,
                                                NULL));

    ComPtr<IMetaDataEmit> pMetadataEmit;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataEmit, reinterpret_cast<IUnknown **>(&pMetadataEmit)));

    IfFailLogRet(ProbeUtilities::InsertProbes(
        m_pCorProfilerInfo,
        pMetadataImport,
        pMetadataEmit,
        pFunctionControl,
        moduleId,
        methodDef,
        functionId,
        enterDef,
        sigParam,
        cbSigParam,
        &typeTokens));

    m_pLogger->Log(LogLevel::Warning, _LS("DONE."));

    // Fix: Always set this, even on error.
    _isRejitting = false;

    return S_OK;
}

HRESULT Snapshot::HydrateResolvedCorLib()
{
    if (m_resolvedCorLibId != 0)
    {
        return S_OK;
    }

    HRESULT hr = S_OK;
    ModuleID candidateModuleId = 0;
    ComPtr<ICorProfilerModuleEnum> pEnum = NULL;
    ModuleID curModule;
    mdTypeDef sysObjectTypeDef = mdTypeDefNil;

    IfFailLogRet(m_pCorProfilerInfo->EnumModules(&pEnum));
    while (pEnum->Next(1, &curModule, NULL) == S_OK)
    {
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

        //
        // Also check the type properties to make sure it is a class and not a Value type definition
        // and that this type definition isn't extending another type.
        //
        bool bExtends = curMetadataImporter->IsValidToken(tkExtends);
        bool isClass = ((dwClassAttrs & tdClassSemanticsMask) == tdClass);
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

    tstring corLibName;
    TypeNameUtilities nameUtilities(m_pCorProfilerInfo);
    IfFailLogRet(nameUtilities.GetModuleNameWithoutCache(candidateModuleId, corLibName));
    
    // .dll = 4 characters
    corLibName.erase(corLibName.length() - 4);

    m_resolvedCorLibName = std::move(corLibName);
    m_resolvedCorLibId = candidateModuleId;
    return S_OK;
}


HRESULT Snapshot::GetTokenForCorLibAssemblyRef(ComPtr<IMetaDataImport> pMetadataImport, ComPtr<IMetaDataEmit> pMetadataEmit, mdAssemblyRef* ptkCorlibAssemblyRef)
{
    HRESULT hr = S_OK;
    *ptkCorlibAssemblyRef = mdAssemblyRefNil;

    IfFailLogRet(HydrateResolvedCorLib());

    ComPtr<IMetaDataAssemblyImport> pMetadataAssemblyImport;
    IfFailLogRet(pMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pMetadataAssemblyImport)));

    HCORENUM hEnum = 0;
    mdAssemblyRef mdRefs[ENUM_BUFFER_SIZE];
    ULONG count = 0;  

    const ULONG expectedLength = (ULONG)m_resolvedCorLibName.length();
    unique_ptr<WCHAR[]> assemblyName(new (nothrow) WCHAR[expectedLength]);
    IfNullRet(assemblyName);

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
                assemblyName.get(), expectedLength, &cchName, // name
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

            tstring assemblyNameStr = tstring(assemblyName.get());
            if (assemblyNameStr == m_resolvedCorLibName) {
                pMetadataAssemblyImport->CloseEnum(hEnum);
                *ptkCorlibAssemblyRef = curRef;
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
        sizeof(publicKeyToken),
        m_resolvedCorLibName.c_str(),
        &corLibMetadata,
        NULL,
        0,
        afContentType_Default,
        ptkCorlibAssemblyRef));

    return S_OK;
}


HRESULT Snapshot::EmitNecessaryCorLibTypeTokens(
    ComPtr<IMetaDataImport> pMetadataImport,
    ComPtr<IMetaDataEmit> pMetadataEmit,
    struct CorLibTypeTokens *pCorLibTypeTokens)
{
    HRESULT hr = S_OK;

    mdAssemblyRef tkCorlibAssemblyRef = mdAssemblyRefNil;
    IfFailLogRet(GetTokenForCorLibAssemblyRef(
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