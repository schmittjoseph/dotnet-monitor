// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "cor.h"
#include "corprof.h"
#include "tstring.h"
#include "../Logging/Logger.h"
#include "../Utilities/NameCache.h"

#include <unordered_map>
#include <memory>

typedef struct _COR_LIB_TYPE_TOKENS
{
    mdToken
        tkSystemBooleanType,
        tkSystemByteType,
        tkSystemCharType,
        tkSystemDoubleType,
        tkSystemInt16Type,
        tkSystemInt32Type,
        tkSystemInt64Type,
        tkSystemObjectType,
        tkSystemSByteType,
        tkSystemSingleType,
        tkSystemStringType,
        tkSystemUInt16Type,
        tkSystemUInt32Type,
        tkSystemUInt64Type,
        tkSystemIntPtrType,
        tkSystemUIntPtrType;
} COR_LIB_TYPE_TOKENS;

class AssemblyProbePrepData
{
public:
    AssemblyProbePrepData(mdMemberRef probeMemberRef, COR_LIB_TYPE_TOKENS corLibTypeTokens) :
        m_probeMemberRef(probeMemberRef), m_corLibTypeTokens(corLibTypeTokens)
    {
    }

    const mdMemberRef GetProbeMemberRef() const { return m_probeMemberRef; }
    const COR_LIB_TYPE_TOKENS GetCorLibTypeTokens() const { return m_corLibTypeTokens; }

private:
    mdMemberRef m_probeMemberRef;
    COR_LIB_TYPE_TOKENS m_corLibTypeTokens;
};


typedef struct _PROBE_CACHE
{
    tstring assemblyName;
    std::unique_ptr<BYTE[]> signature;
    ULONG signatureLength;

    std::unique_ptr<BYTE[]> publicKey;
    ULONG publicKeyLength;

    ASSEMBLYMETADATA assemblyMetadata;
    DWORD assemblyFlags;
} PROBE_CACHE;


class AssemblyProbePrep
{
    private:
        ICorProfilerInfo12* m_pCorProfilerInfo;

        NameCache m_nameCache;

        ModuleID m_resolvedCorLibId;
        tstring m_resolvedCorLibName;

        FunctionID m_probeFunctionId;
        bool m_didHydrateProbeCache;
        PROBE_CACHE m_probeCache;

        std::unordered_map<ModuleID, std::shared_ptr<AssemblyProbePrepData>> m_assemblyProbeCache;

    public:
        AssemblyProbePrep(
            ICorProfilerInfo12* profilerInfo,
            FunctionID probeFunctionId);

        HRESULT PrepareAssemblyForProbes(
            ModuleID moduleId);

        bool TryGetAssemblyPrepData(
            ModuleID moduleId,
            std::shared_ptr<AssemblyProbePrepData>& data);
    
    private:
        HRESULT HydrateResolvedCorLib();
        HRESULT HydrateProbeMetadata();
    
        HRESULT GetTokenForType(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            mdToken tkResolutionScope,
            tstring name,
            mdToken* ptkType);

        HRESULT EmitProbeReference(
            IMetaDataEmit* pMetadataEmit,
            mdMemberRef* ptkProbeMemberRef);

        HRESULT EmitNecessaryCorLibTypeTokens(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            COR_LIB_TYPE_TOKENS* pCorLibTypeTokens);
            
        HRESULT GetTokenForCorLibAssemblyRef(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            mdAssemblyRef* ptkCorlibAssemblyRef);
};