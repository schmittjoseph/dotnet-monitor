// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "AssemblyProbePrep.h"
#include "../Utilities/TypeNameUtilities.h"

#include "corhlpr.h"

using namespace std;

#define ENUM_BUFFER_SIZE 10
#define STRING_BUFFER_LEN 256

AssemblyProbePrep::AssemblyProbePrep(ICorProfilerInfo12* profilerInfo, FunctionID probeFunctionId) :
    m_pCorProfilerInfo(profilerInfo),
    m_resolvedCorLibId(0),
    m_probeFunctionId(probeFunctionId),
    m_didHydrateProbeCache(false)
{
}

bool AssemblyProbePrep::TryGetAssemblyPrepData(ModuleID moduleId, shared_ptr<AssemblyProbePrepData>& data)
{
    auto const& it = m_assemblyProbeCache.find(moduleId);
    if (it != m_assemblyProbeCache.end())
    {
        data = it->second;
        return true;
    }

    return false;
}

HRESULT AssemblyProbePrep::PrepareAssemblyForProbes(ModuleID moduleId)
{
    HRESULT hr;

    auto const& it = m_assemblyProbeCache.find(moduleId);
    if (it != m_assemblyProbeCache.end())
    {
        return S_OK;
    }

    ComPtr<IMetaDataImport> pMetadataImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(
        moduleId,
        ofRead,
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

    COR_LIB_TYPE_TOKENS corLibTypeTokens;
    IfFailRet(EmitNecessaryCorLibTypeTokens(pMetadataImport, pMetadataEmit, &corLibTypeTokens));

    mdMemberRef probeMemberRef;
    IfFailRet(EmitProbeReference(pMetadataEmit, &probeMemberRef));

    shared_ptr<AssemblyProbePrepData> data(new (nothrow) AssemblyProbePrepData(probeMemberRef, corLibTypeTokens));
    IfNullRet(data);
    m_assemblyProbeCache.insert({moduleId, data});

    return S_OK;
}

HRESULT AssemblyProbePrep::EmitProbeReference(
    IMetaDataEmit* pMetadataEmit,
    mdMemberRef* ptkProbeMemberRef)
{
    IfNullRet(pMetadataEmit);
    IfNullRet(ptkProbeMemberRef);

    HRESULT hr;
    *ptkProbeMemberRef = mdMemberRefNil;

    IfFailRet(HydrateProbeMetadata());

    std::shared_ptr<FunctionData> probeFunctionData;
    std::shared_ptr<ModuleData> probeModuleData;
    if (!m_nameCache.TryGetFunctionData(m_probeFunctionId, probeFunctionData) ||
        !m_nameCache.TryGetModuleData(probeFunctionData->GetModuleId(), probeModuleData))
    {
        return E_UNEXPECTED;
    }

    ComPtr<IMetaDataAssemblyEmit> pMetadataAssemblyEmit;
    mdAssemblyRef tkProbeAssemblyRef = mdAssemblyRefNil;
    IfFailRet(pMetadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));
    IfFailRet(pMetadataAssemblyEmit->DefineAssemblyRef(
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
    IfFailRet(pMetadataEmit->DefineTypeRefByName(tkProbeAssemblyRef, className.c_str(), &refToken));
    IfFailRet(pMetadataEmit->DefineMemberRef(refToken, probeFunctionData->GetName().c_str(), m_probeCache.signature.get(), m_probeCache.signatureLength, ptkProbeMemberRef));

    return S_OK;
}

HRESULT AssemblyProbePrep::EmitNecessaryCorLibTypeTokens(
    IMetaDataImport* pMetadataImport,
    IMetaDataEmit* pMetadataEmit,
    COR_LIB_TYPE_TOKENS* pCorLibTypeTokens)
{
    IfNullRet(pMetadataImport);
    IfNullRet(pMetadataEmit);
    IfNullRet(pCorLibTypeTokens);

    HRESULT hr;

    mdAssemblyRef tkCorlibAssemblyRef = mdAssemblyRefNil;
    IfFailRet(GetTokenForCorLibAssemblyRef(
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

HRESULT AssemblyProbePrep::GetTokenForType(
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

HRESULT AssemblyProbePrep::GetTokenForCorLibAssemblyRef(IMetaDataImport* pMetadataImport, IMetaDataEmit* pMetadataEmit, mdAssemblyRef* ptkCorlibAssemblyRef)
{
    IfNullRet(pMetadataImport);
    IfNullRet(pMetadataEmit);
    IfNullRet(ptkCorlibAssemblyRef);

    HRESULT hr;
    *ptkCorlibAssemblyRef = mdAssemblyRefNil;

    IfFailRet(HydrateResolvedCorLib());

    ComPtr<IMetaDataAssemblyImport> pMetadataAssemblyImport;
    IfFailRet(pMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pMetadataAssemblyImport)));

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
    IfFailRet(pMetadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));

    BYTE publicKeyToken[] = { 0x7c, 0xec, 0x85, 0xd7, 0xbe, 0xa7, 0x79, 0x8e };
    ASSEMBLYMETADATA corLibMetadata = {};
    corLibMetadata.usMajorVersion = 4;

    IfFailRet(pMetadataAssemblyEmit->DefineAssemblyRef(
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

HRESULT AssemblyProbePrep::HydrateResolvedCorLib()
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

    IfFailRet(m_pCorProfilerInfo->EnumModules(&pEnum));
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

HRESULT AssemblyProbePrep::HydrateProbeMetadata()
{
    if (m_didHydrateProbeCache)
    {
        return S_OK;
    }

    HRESULT hr;
    TypeNameUtilities typeNameUtilities(m_pCorProfilerInfo);
    IfFailRet(typeNameUtilities.CacheNames(m_nameCache, m_probeFunctionId, NULL));

    std::shared_ptr<FunctionData> probeFunctionData;
    std::shared_ptr<ModuleData> probeModuleData;
    if (!m_nameCache.TryGetFunctionData(m_probeFunctionId, probeFunctionData) ||
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
    IfFailRet(pProbeMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pProbeAssemblyImport)));
    mdAssembly tkProbeAssembly;
    IfFailRet(pProbeAssemblyImport->GetAssemblyFromScope(&tkProbeAssembly));

    const BYTE *pPublicKey;
    ULONG publicKeyLength = 0;
    ASSEMBLYMETADATA metadata = {};
    WCHAR szName[STRING_BUFFER_LEN];
    ULONG actualLength = 0;
    IfFailRet(pProbeAssemblyImport->GetAssemblyProps(
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

    m_didHydrateProbeCache = true;
    return S_OK;
}
