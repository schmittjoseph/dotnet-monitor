// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "ProbeInstrumentation.h"
#include <functional>
#include <memory>
#include "ProbeInjector.h"
#include "../Utilities/TypeNameUtilities.h"

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

#define ENUM_BUFFER_SIZE 10
#define STRING_BUFFER_LEN 256

ProbeInstrumentation::ProbeInstrumentation(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo) :
    m_pCorProfilerInfo(profilerInfo),
    m_pLogger(logger),
    m_didHydrateCache(false),
    m_resolvedCorLibId(0)
{
    m_probeCache.functionId = 0;
}

HRESULT ProbeInstrumentation::RegisterFunctionProbe(FunctionID enterProbeId)
{
    if (IsAvailable())
    {
        // Probes have already been pinned.
        return E_FAIL;
    }

    m_pLogger->Log(LogLevel::Information, _LS("Received probes."));

    // JSFIX: Validate the probe's signature before pinning it.
    m_probeCache.functionId = enterProbeId;
    
    return S_OK;
}

HRESULT ProbeInstrumentation::InitBackgroundService()
{
    m_probeManagementThread = thread(&ProbeInstrumentation::WorkerThread, this);
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
        WORKER_PAYLOAD payload;
        hr = m_probeManagementQueue.BlockingDequeue(payload);
        if (hr != S_OK)
        {
            break;
        }

        switch (payload.message)
        {
        case INSTALL_PROBES:
            hr = Enable(payload.requests);
            if (hr != S_OK)
            {
                m_pLogger->Log(LogLevel::Error, _LS("Failed to install probes: 0x%08x"), hr);
            }
            break;

        case UNINSTALL_PROBES:
            hr = Disable();
            if (hr != S_OK)
            {
                m_pLogger->Log(LogLevel::Error, _LS("Failed to uninstall probes: 0x%08x"), hr);
            }
            break;

        default:
            m_pLogger->Log(LogLevel::Error, _LS("Unknown message"));
            break;
        }
    }
}

void ProbeInstrumentation::ShutdownBackgroundService()
{
    m_probeManagementQueue.Complete();
    m_probeManagementThread.join();
}

HRESULT ProbeInstrumentation::RequestFunctionProbeInstallation(UINT64 functionIds[], ULONG count, UINT32 boxingTokens[], ULONG boxingTokenCounts[])
{
    m_pLogger->Log(LogLevel::Information, _LS("Probe installation requested"));

    vector<UNPROCESSED_INSTRUMENTATION_REQUEST> requests;
    requests.reserve(count);

    ULONG offset = 0;
    for (ULONG i = 0; i < count; i++)
    {
        vector<UINT32> tokens;
        ULONG j;
        for (j = 0; j < boxingTokenCounts[i]; j++)
        {
            tokens.push_back(boxingTokens[offset+j]);
        }

        offset += j;

        UNPROCESSED_INSTRUMENTATION_REQUEST request;
        request.functionId = (FunctionID)functionIds[i];
        request.tkBoxingTypes = tokens;

        requests.push_back(request);
    }

    m_probeManagementQueue.Enqueue({WorkerMessage::INSTALL_PROBES, requests});

    return S_OK;
}

HRESULT ProbeInstrumentation::RequestFunctionProbeShutdown()
{
    m_pLogger->Log(LogLevel::Debug, _LS("Probe shutdown requested"));

    if (!IsAvailable())
    {
        return S_FALSE;
    }

    WORKER_PAYLOAD payload = {};
    payload.message = WorkerMessage::UNINSTALL_PROBES;
    m_probeManagementQueue.Enqueue(payload);

    return S_OK;
}

BOOL ProbeInstrumentation::IsAvailable()
{
    return m_probeCache.functionId != 0;
}

HRESULT ProbeInstrumentation::Enable(vector<UNPROCESSED_INSTRUMENTATION_REQUEST>& requests)
{
    HRESULT hr;

    lock_guard<mutex> lock(m_requestProcessingMutex);

    if (!IsAvailable() ||
        IsEnabled())
    {
        return E_FAIL;
    }

    IfFailLogRet(HydrateProbeMetadata());

    unordered_map<pair<ModuleID, mdMethodDef>, INSTRUMENTATION_REQUEST, PairHash<ModuleID, mdMethodDef>> newRequests;

    vector<ModuleID> requestedModuleIds;
    vector<mdMethodDef> requestedMethodDefs;

    requestedModuleIds.reserve(requests.size());
    requestedMethodDefs.reserve(requests.size());

    for (auto const& req : requests)
    {
        INSTRUMENTATION_REQUEST request;
        request.uniquifier = (UINT64)req.functionId;
        request.tkBoxingTypes = req.tkBoxingTypes;

        IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(
            req.functionId,
            NULL,
            nullptr,
            &request.moduleId,
            &request.methodDef,
            0,
            nullptr,
            nullptr));

        mdMemberRef tkProbeFunction = mdMemberRefNil;
        IfFailLogRet(PrepareAssemblyForProbes(request.moduleId, request.methodDef, request.assemblyProbeInformation));

        requestedModuleIds.push_back(request.moduleId);
        requestedMethodDefs.push_back(request.methodDef);

        newRequests.insert({{request.moduleId, request.methodDef}, request});
    }

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)requestedModuleIds.size(),
        requestedModuleIds.data(),
        requestedMethodDefs.data()));

    m_activeInstrumentationRequests = newRequests;

    return S_OK;
}


HRESULT ProbeInstrumentation::PrepareAssemblyForProbes(ModuleID moduleId, mdMethodDef methodId, shared_ptr<ASSEMBLY_PROBE_CACHE_ENTRY>& assemblyProbeInformation)
{
    HRESULT hr;

    auto const& it = m_AssemblyProbeCache.find(moduleId);
    if (it != m_AssemblyProbeCache.end())
    {
        assemblyProbeInformation = it->second;
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
        return hr;
    }
    
    ComPtr<IMetaDataEmit> pMetadataEmit;
    hr = m_pCorProfilerInfo->GetModuleMetaData(
        moduleId,
        ofRead | ofWrite,
        IID_IMetaDataEmit,
        reinterpret_cast<IUnknown **>(&pMetadataEmit));
    if (hr != S_OK)
    {
        return hr;
    }

    shared_ptr<ASSEMBLY_PROBE_CACHE_ENTRY> cacheEntry = make_shared<ASSEMBLY_PROBE_CACHE_ENTRY>();
    IfFailLogRet(EmitNecessaryCorLibTypeTokens(pMetadataImport, pMetadataEmit, &cacheEntry->corLibTypeTokens));
    IfFailLogRet(EmitProbeReference(pMetadataEmit, &cacheEntry->tkProbeMemberRef));

    auto cacheItr = m_AssemblyProbeCache.insert({moduleId, cacheEntry}).first;
    assemblyProbeInformation = cacheItr->second;

    return S_OK;
}


HRESULT ProbeInstrumentation::Disable()
{
    HRESULT hr;

    lock_guard<mutex> lock(m_requestProcessingMutex);

    if (!IsEnabled())
    {
        return S_FALSE;
    }

    vector<ModuleID> moduleIds;
    vector<mdMethodDef> methodDefs;

    moduleIds.reserve(m_activeInstrumentationRequests.size());
    methodDefs.reserve(m_activeInstrumentationRequests.size());

    for (auto const& requestData: m_activeInstrumentationRequests)
    {
        auto const& methodInfo = requestData.first;
        moduleIds.push_back(methodInfo.first);
        methodDefs.push_back(methodInfo.second);
    }

    IfFailLogRet(m_pCorProfilerInfo->RequestRevert(
        (ULONG)moduleIds.size(),
        moduleIds.data(),
        methodDefs.data(),
        nullptr));

    m_activeInstrumentationRequests.clear();

    return S_OK;
}

BOOL ProbeInstrumentation::IsEnabled()
{
    return !m_activeInstrumentationRequests.empty();
}

void ProbeInstrumentation::AddProfilerEventMask(DWORD& eventsLow)
{
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT;
}

HRESULT STDMETHODCALLTYPE ProbeInstrumentation::GetReJITParameters(ModuleID moduleId, mdMethodDef methodDef, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr;

    INSTRUMENTATION_REQUEST* pRequest;
    {
        lock_guard<mutex> lock(m_requestProcessingMutex);
        auto const& it = m_activeInstrumentationRequests.find({moduleId, methodDef});
        if (it == m_activeInstrumentationRequests.end())
        {
            return E_FAIL;
        }
        pRequest = &it->second;
    }

    hr = ProbeInjector::InstallProbe(
        m_pCorProfilerInfo,
        pFunctionControl,
        pRequest);

    if (FAILED(hr))
    {
        RequestFunctionProbeShutdown();
        return hr;
    }

    return S_OK;
}

HRESULT ProbeInstrumentation::HydrateProbeMetadata()
{
    if (m_didHydrateCache)
    {
        return S_OK;
    }

    HRESULT hr;
    TypeNameUtilities typeNameUtilities(m_pCorProfilerInfo);
    IfFailRet(typeNameUtilities.CacheNames(m_nameCache, m_probeCache.functionId, NULL));

    std::shared_ptr<FunctionData> probeFunctionData;
    std::shared_ptr<ModuleData> probeModuleData;
    if (!m_nameCache.TryGetFunctionData(m_probeCache.functionId, probeFunctionData) ||
        !m_nameCache.TryGetModuleData(probeFunctionData->GetModuleId(), probeModuleData))
    {
        return E_UNEXPECTED;
    }

    ComPtr<IMetaDataImport> pProbeMetadataImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(probeFunctionData->GetModuleId(), ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pProbeMetadataImport));
    if (hr != S_OK)
    {
        return hr;
    }

    ComPtr<IMetaDataAssemblyImport> pProbeAssemblyImport;
    IfFailLogRet(pProbeMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pProbeAssemblyImport)));
    mdAssembly tkProbeAssembly;
    IfFailLogRet(pProbeAssemblyImport->GetAssemblyFromScope(&tkProbeAssembly));

    const BYTE *pPublicKey;
    ULONG publicKeyLength = 0;
    ASSEMBLYMETADATA metadata = {};
    WCHAR szName[STRING_BUFFER_LEN];
    ULONG actualLength = 0;
    IfFailLogRet(pProbeAssemblyImport->GetAssemblyProps(
        tkProbeAssembly,
        (const void **)&pPublicKey,
        &m_probeCache.publicKeyLength,
        nullptr,
        szName,
        STRING_BUFFER_LEN,
        &actualLength,
        &metadata,
        &m_probeCache.assemblyFlags));

    m_probeCache.assemblyMetadata = metadata;
    m_probeCache.assemblyName = tstring(szName);
    m_probeCache.publicKey.reset(new (nothrow) BYTE[m_probeCache.publicKeyLength ]);
    IfNullRet(m_probeCache.publicKey);
    memcpy_s(m_probeCache.publicKey.get(), m_probeCache.publicKeyLength, pPublicKey, m_probeCache.publicKeyLength );

    PCCOR_SIGNATURE pProbeSignature;
    IfFailRet(pProbeMetadataImport->GetMethodProps(
        probeFunctionData->GetMethodToken(),
        nullptr,
        nullptr,
        NULL,
        nullptr,
        nullptr,
        &pProbeSignature,
        &m_probeCache.signatureLength,
        nullptr,
        nullptr));

    m_probeCache.signature.reset(new (nothrow) BYTE[m_probeCache.signatureLength]);
    IfNullRet(m_probeCache.signature);
    memcpy_s(m_probeCache.signature.get(), m_probeCache.signatureLength, pProbeSignature, m_probeCache.signatureLength);

    m_didHydrateCache = true;

    return S_OK;
}

HRESULT ProbeInstrumentation::HydrateResolvedCorLib()
{
    if (m_resolvedCorLibId != 0)
    {
        return S_OK;
    }

    HRESULT hr;
    ModuleID candidateModuleId = 0;
    ComPtr<ICorProfilerModuleEnum> pEnum = NULL;
    ModuleID curModule;
    mdTypeDef sysObjectTypeDef = mdTypeDefNil;

    IfFailLogRet(m_pCorProfilerInfo->EnumModules(&pEnum));
    while (pEnum->Next(1, &curModule, NULL) == S_OK)
    {
        //
        // Determine the identity of the System assembly by querying if the Assembly defines the
        // well known type "System.Object" as that type must be defined by the System assembly
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
        // Also check the type properties to make sure it is a class and not a value-type definition
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
    nameUtilities.CacheModuleNames(m_nameCache, candidateModuleId);

    std::shared_ptr<ModuleData> moduleData;
    if (!m_nameCache.TryGetModuleData(candidateModuleId, moduleData))
    {
        return E_UNEXPECTED;
    }

    corLibName = moduleData->GetName();
   
    // .dll = 4 characters
    corLibName.erase(corLibName.length() - 4);

    m_resolvedCorLibName = move(corLibName);
    m_resolvedCorLibId = candidateModuleId;
    return S_OK;
}

HRESULT ProbeInstrumentation::GetTokenForCorLibAssemblyRef(IMetaDataImport* pMetadataImport, IMetaDataEmit* pMetadataEmit, mdAssemblyRef* ptkCorlibAssemblyRef)
{
    IfNullRet(pMetadataImport);
    IfNullRet(pMetadataEmit);
    IfNullRet(ptkCorlibAssemblyRef);

    HRESULT hr;
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
    ASSEMBLYMETADATA corLibMetadata = {};
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
    IMetaDataEmit* pMetadataEmit,
    mdMemberRef* ptkProbeMemberRef)
{
    IfNullRet(pMetadataEmit);
    IfNullRet(ptkProbeMemberRef);

    HRESULT hr;
    *ptkProbeMemberRef = mdMemberRefNil;

    std::shared_ptr<FunctionData> probeFunctionData;
    std::shared_ptr<ModuleData> probeModuleData;
    if (!m_nameCache.TryGetFunctionData(m_probeCache.functionId, probeFunctionData) ||
        !m_nameCache.TryGetModuleData(probeFunctionData->GetModuleId(), probeModuleData))
    {
        return E_UNEXPECTED;
    }

    ComPtr<IMetaDataAssemblyEmit> pMetadataAssemblyEmit;
    mdAssemblyRef tkProbeAssemblyRef = mdAssemblyRefNil;
    IfFailLogRet(pMetadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));
    IfFailLogRet(pMetadataAssemblyEmit->DefineAssemblyRef(
        (const void *)m_probeCache.publicKey.get(),
        m_probeCache.publicKeyLength,
        m_probeCache.assemblyName.c_str(),
        &m_probeCache.assemblyMetadata,
        NULL,
        0,
        m_probeCache.assemblyFlags,
        &tkProbeAssemblyRef));

    tstring className;
    IfFailRet(m_nameCache.GetFullyQualifiedClassName(probeFunctionData->GetClass(), className));

    mdTypeRef refToken = mdTokenNil;
    IfFailLogRet(pMetadataEmit->DefineTypeRefByName(tkProbeAssemblyRef, className.c_str(), &refToken));
    IfFailLogRet(pMetadataEmit->DefineMemberRef(refToken, probeFunctionData->GetName().c_str(), m_probeCache.signature.get(), m_probeCache.signatureLength, ptkProbeMemberRef));

    return S_OK;
}

HRESULT ProbeInstrumentation::EmitNecessaryCorLibTypeTokens(
    IMetaDataImport* pMetadataImport,
    IMetaDataEmit* pMetadataEmit,
    COR_LIB_TYPE_TOKENS* pCorLibTypeTokens)
{
    IfNullRet(pMetadataImport);
    IfNullRet(pMetadataEmit);
    IfNullRet(pCorLibTypeTokens);

    HRESULT hr;

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
    IMetaDataImport* pMetadataImport,
    IMetaDataEmit* pMetadataEmit,
    mdToken tkResolutionScope,
    tstring name,
    mdToken* ptkType)
{
    IfNullRet(pMetadataImport);
    IfNullRet(pMetadataEmit);
    IfNullRet(ptkType);

    HRESULT hr;

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