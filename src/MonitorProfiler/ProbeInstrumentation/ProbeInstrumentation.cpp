// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "ProbeInstrumentation.h"
#include <functional>
#include <memory>
#include "ProbeInjector.h"
#include "MethodSignatureParser.h"
#include "../Utilities/NameCache.h"
#include "../Utilities/TypeNameUtilities.h"

#include "JSFixUtils.h"
#define IfFailLogRet(EXPR) JSFIX_IfFailLogAndBreak_(m_pLogger, EXPR)

using namespace std;

#define ENUM_BUFFER_SIZE 10

ProbeInstrumentation::ProbeInstrumentation(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo) :
    m_pCorProfilerInfo(profilerInfo),
    m_pLogger(logger),
    m_enterProbeId(0),
    m_enterProbeDef(mdMethodDefNil),
    m_resolvedCorLibId(0),
    _isRejitting(false),
    _isEnabled(false)
{
}

HRESULT ProbeInstrumentation::RegisterFunctionProbe(FunctionID enterProbeId)
{
    if (IsAvailable())
    {
        // Probes have already been pinned.
        return E_FAIL;
    }

    m_pLogger->Log(LogLevel::Information, _LS("Received probes."));


    m_enterProbeId = enterProbeId;
    
    return S_OK;
}

HRESULT ProbeInstrumentation::InitBackgroundService()
{
    _workerThread = std::thread(&ProbeInstrumentation::WorkerThread, this);
    return S_OK;
}

void ProbeInstrumentation::WorkerThread()
{
    HRESULT hr = m_pCorProfilerInfo->InitializeCurrentThread();
    if (FAILED(hr))
    {
        m_pLogger->Log(LogLevel::Error, _LS("Unable to initialize thread: 0x%08x"), hr);
        return;
    }

    while (true)
    {
        WorkerMessage message;
        hr = _workerQueue.BlockingDequeue(message);
        if (hr != S_OK)
        {
            //We are complete, discard all messages
            break;
        }

        switch (message)
        {
        case INSTALL_PROBES:
            hr = Enable();
            if (hr != S_OK)
            {
                m_pLogger->Log(LogLevel::Error, _LS("Failed to install probes: 0x%08x"), hr);
                TEMPORARY_BREAK_ON_ERROR();
            }
            break;

        case UNINSTALL_PROBES:
            hr = Disable();
            if (hr != S_OK)
            {
                m_pLogger->Log(LogLevel::Error, _LS("Failed to uninstall probes: 0x%08x"), hr);
                TEMPORARY_BREAK_ON_ERROR();
            }
            break;

        default:
            m_pLogger->Log(LogLevel::Error, _LS("Unknown message"));
            TEMPORARY_BREAK_ON_ERROR();
            break;
        }
    }
}

void ProbeInstrumentation::ShutdownBackgroundService()
{
    // JSFIX: Make re-entrant
    _workerQueue.Complete();
    _workerThread.join();
}

HRESULT ProbeInstrumentation::RequestFunctionProbeInstallation(UINT64 functionIds[], ULONG count, UINT32 boxingTokens[], ULONG boxingTokenCounts[])
{
    m_pLogger->Log(LogLevel::Information, _LS("Requesting probe installation."));

    // JSFIX: Not thread safe
    ULONG offset = 0;
    m_RequestedFunctionIds.clear();
    for (ULONG i = 0; i < count; i++)
    {
        std::vector<UINT32> tokens;
        ULONG j;
        for (j = 0; j < boxingTokenCounts[i]; j++)
        {
            tokens.push_back(boxingTokens[offset+j]);
        }
        offset += j;

        m_RequestedFunctionIds.push_back({(FunctionID)functionIds[i], tokens});
    }

    _workerQueue.Enqueue(WorkerMessage::INSTALL_PROBES);

    return S_OK;
}

HRESULT ProbeInstrumentation::RequestFunctionProbeShutdown()
{
    if (!IsAvailable())
    {
        return S_FALSE;
    }

    m_pLogger->Log(LogLevel::Information, _LS("Uninstall probes requested"));
    _workerQueue.Enqueue(WorkerMessage::UNINSTALL_PROBES);

    return S_OK;
}

BOOL ProbeInstrumentation::IsAvailable()
{
    return m_enterProbeId != 0;
}

HRESULT ProbeInstrumentation::Enable()
{
    HRESULT hr = S_OK;

    if (!IsAvailable() ||
        IsEnabled() ||
        m_RequestedFunctionIds.size() == 0 ||
        m_RequestedFunctionIds.size() > ULONG_MAX)
    {
        return E_FAIL;
    }

    std::unordered_map<std::pair<ModuleID, mdMethodDef>, struct InstrumentationRequest, PairHash<ModuleID, mdMethodDef>> newRequests;

    IfFailLogRet(HydrateProbeMetadata());

    std::vector<ModuleID> requestedModuleIds;
    std::vector<mdMethodDef> requestedMethodDefs;

    for (ULONG i = 0; i < m_RequestedFunctionIds.size(); i++)
    {
        auto const funcInfo = m_RequestedFunctionIds.at(i);

        struct InstrumentationRequest request;

        request.functionId = funcInfo.first;
        request.tkBoxingTypes = funcInfo.second;

        IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(
            request.functionId,
            NULL,
            nullptr,
            &request.moduleId,
            &request.methodDef,
            0,
            nullptr,
            nullptr));

        mdMemberRef tkProbeFunction = mdMemberRefNil;
        IfFailLogRet(PrepareAssemblyForProbes(request.moduleId, request.methodDef, &request.probeFunctionDef));

        std::vector<std::pair<CorElementType, mdToken>> paramTypes;

        IfFailLogRet(MethodSignatureParser::GetMethodSignatureParamTypes(
            m_pCorProfilerInfo,
            request.moduleId,
            request.methodDef,
            request.hasThis,
            request.paramTypes));
        
        requestedModuleIds.push_back(request.moduleId);
        requestedMethodDefs.push_back(request.methodDef);

        newRequests.insert({{request.moduleId, request.methodDef}, request});
    }

    _isRejitting = true;

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)requestedModuleIds.size(),
        requestedModuleIds.data(),
        requestedMethodDefs.data()));

    m_RequestedFunctionIds.clear();
    m_InstrumentationRequests = newRequests;
    _isEnabled = true;

    return S_OK;
}


HRESULT ProbeInstrumentation::PrepareAssemblyForProbes(ModuleID moduleId, mdMethodDef methodId, mdMemberRef* ptkProbeMemberRef)
{
    HRESULT hr;

    auto const& it = m_ModuleTokens.find(moduleId);
    if (it != m_ModuleTokens.end())
    {
        return S_OK;
    }

    ComPtr<IMetaDataImport> pMetadataImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(
        moduleId,
        ofRead | ofWrite,
        IID_IMetaDataImport,
        reinterpret_cast<IUnknown **>(&pMetadataImport));
    if (hr != S_OK)
    {
        TEMPORARY_BREAK_ON_ERROR();
        return hr;
    }
    
    ComPtr<IMetaDataEmit> pMetadataEmit;
    IfFailRet(pMetadataImport->QueryInterface(IID_IMetaDataEmit, reinterpret_cast<void **>(&pMetadataEmit)));

    struct CorLibTypeTokens corLibTypeTokens;
    IfFailLogRet(EmitNecessaryCorLibTypeTokens(pMetadataImport, pMetadataEmit, &corLibTypeTokens));
    IfFailLogRet(EmitProbeReference(pMetadataImport, pMetadataEmit, ptkProbeMemberRef));

    m_ModuleTokens.insert({moduleId, corLibTypeTokens});

    return S_OK;
}


HRESULT ProbeInstrumentation::Disable()
{
    HRESULT hr = S_OK;

    if (!IsEnabled() || _isRejitting)
    {
        return S_FALSE;
    }

    m_pLogger->Log(LogLevel::Warning, _LS("Disabling probes"));

    // JSFIX: Keep this data on-hand.
    // JSFIX: Not thread safe.

    std::vector<ModuleID> moduleIds;
    std::vector<mdMethodDef> methodDefs;

    for (auto const requestData: m_InstrumentationRequests)
    {
        auto const f = requestData.first;
        moduleIds.push_back(f.first);
        methodDefs.push_back(f.second);
    }

    // JSFIX: Validate that if this method returns S_OK then we don't have to check
    // the return statuses.
    IfFailLogRet(m_pCorProfilerInfo->RequestRevert(
        (ULONG)moduleIds.size(),
        moduleIds.data(),
        methodDefs.data(),
        nullptr));

    m_InstrumentationRequests.clear();

    _isEnabled = false;

    return S_OK;
}

BOOL ProbeInstrumentation::IsEnabled() {
    return _isEnabled;
}

void ProbeInstrumentation::AddProfilerEventMask(DWORD& eventsLow)
{
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT | COR_PRF_MONITOR::COR_PRF_MONITOR_JIT_COMPILATION;
}

HRESULT STDMETHODCALLTYPE ProbeInstrumentation::ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    // JSFIX: Handle accordingly.
    return S_OK;
} 

HRESULT STDMETHODCALLTYPE ProbeInstrumentation::ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus)
{
    // JSFIX: Handle accordingly.
    m_pLogger->Log(LogLevel::Error, _LS("ReJIT error - 0x%08x - hr:0x%08x"), functionId, hrStatus);
    TEMPORARY_BREAK_ON_ERROR();
    return S_OK;
} 

HRESULT STDMETHODCALLTYPE ProbeInstrumentation::GetReJITParameters(ModuleID moduleId, mdMethodDef methodDef, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr = S_OK;

    struct CorLibTypeTokens typeTokens;
    auto const& it = m_ModuleTokens.find(moduleId);
    if (it == m_ModuleTokens.end())
    {
        return E_FAIL;
    }
    typeTokens = it->second;

    struct InstrumentationRequest request;
    auto const& it2 = m_InstrumentationRequests.find({moduleId, methodDef});
    if (it2 == m_InstrumentationRequests.end())
    {
        return E_FAIL;
    }
    request = it2->second;

    IfFailLogRet(ProbeInjector::InstallProbe(
        m_pCorProfilerInfo,
        pFunctionControl,
        &request,
        &typeTokens));

    // JSFIX: Always set this, even on error.
    _isRejitting = false;

    return S_OK;
}

HRESULT ProbeInstrumentation::HydrateProbeMetadata()
{
    if (m_resolvedCorLibId != 0)
    {
        return S_OK;
    }

    HRESULT hr;
    mdMethodDef enterDef = mdTokenNil;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(m_enterProbeId,
                                                NULL,
                                                nullptr,
                                                nullptr,
                                                &enterDef,
                                                0,
                                                nullptr,
                                                nullptr));

    m_enterProbeDef = enterDef;
    return S_OK;
}

HRESULT ProbeInstrumentation::HydrateResolvedCorLib()
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
        hr = m_pCorProfilerInfo->GetModuleMetaData(curModule, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&curMetadataImporter));
        if (hr != S_OK)
        {
            continue;
        }

        if (curMetadataImporter->FindTypeDefByName(_T("System.Object"), mdTokenNil, &tkObjectTypeDef) != S_OK)
        {
            continue;
        }

        DWORD dwClassAttrs = 0;
        mdToken tkExtends = mdTokenNil;
        if (curMetadataImporter->GetTypeDefProps(tkObjectTypeDef, nullptr, 0, nullptr, &dwClassAttrs, &tkExtends) != S_OK)
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


HRESULT ProbeInstrumentation::GetTokenForCorLibAssemblyRef(ComPtr<IMetaDataImport> pMetadataImport, ComPtr<IMetaDataEmit> pMetadataEmit, mdAssemblyRef* ptkCorlibAssemblyRef)
{
    HRESULT hr = S_OK;
    *ptkCorlibAssemblyRef = mdAssemblyRefNil;

    IfFailLogRet(HydrateResolvedCorLib());

    ComPtr<IMetaDataAssemblyImport> pMetadataAssemblyImport;
    IfFailLogRet(pMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pMetadataAssemblyImport)));

    // JSFIX: Consider RAII for scope guarding instead of closing this enum in all needed cases.
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
                nullptr, nullptr, // public key or token
                assemblyName.get(), expectedLength, &cchName, // name
                nullptr, // metadata`
                nullptr, nullptr, // hash value
                nullptr); // flags

            // Current assembly's name is longer than corlib's
            if (hr == CLDB_S_TRUNCATION)
            {
                continue;
            }
            else if (hr != S_OK)
            {
                pMetadataAssemblyImport->CloseEnum(hEnum);
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
        nullptr,
        0,
        afContentType_Default,
        ptkCorlibAssemblyRef));

    return S_OK;
}

HRESULT ProbeInstrumentation::EmitProbeReference(
    ComPtr<IMetaDataImport> pMetadataImport,
    ComPtr<IMetaDataEmit> pMetadataEmit,
    mdMemberRef* ptkProbeMemberRef)
{
    HRESULT hr;
    *ptkProbeMemberRef = mdMemberRefNil;

    mdAssemblyRef tkProbeAssemblyRef = mdAssemblyRefNil;

    ModuleID probeModuleId = 0;
    IfFailRet(m_pCorProfilerInfo->GetFunctionInfo2(m_enterProbeId,
        NULL,
        nullptr,
        &probeModuleId,
        nullptr,
        0,
        nullptr,
        nullptr));

    ComPtr<IMetaDataImport> pProbeMetadataImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(probeModuleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pProbeMetadataImport));
    if (hr != S_OK)
    {
        TEMPORARY_BREAK_ON_ERROR();
        return hr;
    }

    PCCOR_SIGNATURE pProbeSignature;
    ULONG probeSignatureLength;
    WCHAR funcName[256];

    IfFailRet(pProbeMetadataImport->GetMethodProps(
        m_enterProbeDef,
        nullptr,
        funcName,
        256,
        nullptr,
        nullptr,
        &pProbeSignature,
        &probeSignatureLength,
        nullptr,
        nullptr));


    ComPtr<IMetaDataAssemblyImport> pProbeAssemblyImport;
    IfFailLogRet(pProbeMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pProbeAssemblyImport)));
    mdAssembly tkProbeAssembly;
    IfFailLogRet(pProbeAssemblyImport->GetAssemblyFromScope(&tkProbeAssembly));
    
    const BYTE      *pbPublicKey;
    ULONG           ulHashAlgId;
    ULONG           cbPublicKey = 0;
    ASSEMBLYMETADATA MetaData = { 0 };
    #define STRING_BUFFER_LEN 256
    WCHAR           szName[STRING_BUFFER_LEN];
    ULONG actualLength = 0;
    DWORD           dwFlags = 0;
    IfFailLogRet(pProbeAssemblyImport->GetAssemblyProps(tkProbeAssembly, (const void **)&pbPublicKey, &cbPublicKey, &ulHashAlgId, szName, STRING_BUFFER_LEN, &actualLength, &MetaData, &dwFlags));

    // TODO: Hash

    // We loaded with a no pub key.
    ComPtr<IMetaDataAssemblyEmit> pMetadataAssemblyEmit;
    IfFailLogRet(pMetadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));
    IfFailLogRet(pMetadataAssemblyEmit->DefineAssemblyRef(
        (const void *)pbPublicKey,
        cbPublicKey,
        szName,
        &MetaData,
        NULL,
        0,
        dwFlags,
        &tkProbeAssemblyRef));

    // JSFIX: Resolve class name dynamically
    mdTypeRef refToken = mdTokenNil;
    IfFailLogRet(pMetadataEmit->DefineTypeRefByName(tkProbeAssemblyRef, _T("Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter.Probes"), &refToken));

    IfFailLogRet(pMetadataEmit->DefineMemberRef(refToken, funcName, pProbeSignature, probeSignatureLength, ptkProbeMemberRef));

    return S_OK;
}

HRESULT ProbeInstrumentation::EmitNecessaryCorLibTypeTokens(
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

HRESULT ProbeInstrumentation::GetTokenForType(
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