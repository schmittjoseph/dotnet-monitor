// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "CorLibTypeTokens.h"
#include <vector>

class ProbeUtilities
{
    public:
        static HRESULT InsertProbes(
            ICorProfilerInfo* pICorProfilerInfo,
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            ICorProfilerFunctionControl* pICorProfilerFunctionControl,
            ModuleID moduleID,
            mdMethodDef methodDef,
            FunctionID functionId,
            mdMethodDef enterProbeDef,
            PCCOR_SIGNATURE sigParam,
            ULONG cbSigParam,
            struct CorLibTypeTokens* pCorLibTypeTokens);

    private:
        static HRESULT DecompressNextSigComponent(
            IMetaDataEmit* pMetadataEmit,
            PCCOR_SIGNATURE pSignature,
            ULONG signatureLength,
            ULONG *pBytesRead,
            CorElementType* pElementType,
            mdToken* ptkType);

        static HRESULT ProcessArgs(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            PCCOR_SIGNATURE pbSigBlob,
            ULONG signatureLength,
            BOOL* hasThis,
            std::vector<std::pair<CorElementType, mdToken>>& paramTypes);
        
        static HRESULT GetTypeToBoxWith(
            IMetaDataImport* pMetadataImport,
            std::pair<CorElementType, mdToken> typeInfo,
            mdTypeDef* ptkBoxedType,
            struct CorLibTypeTokens* pCorLibTypeTokens);
};